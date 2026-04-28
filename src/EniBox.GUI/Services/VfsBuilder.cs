using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public sealed class VfsBuilder : IVfsBuilder
    {
        private class VfsDirNode
        {
            public string Name { get; set; } = string.Empty;
            public VfsDirNode? Parent { get; set; }
            public List<VfsDirNode> Children { get; } = new();
            public List<PackFileItem> Files { get; } = new();
        }

        private readonly VfsDirNode _root = new() { Name = "" };
        private readonly ICompressor _compressor;
        private readonly List<PackFileItem> _allFiles = new();

        public VfsBuilder(ICompressor compressor)
        {
            _compressor = compressor;
        }

        public void AddFile(PackFileItem file)
        {
            _allFiles.Add(file);
            var dir = EnsureDirectory(Path.GetDirectoryName(file.VirtualPath) ?? "");
            dir.Files.Add(file);
        }

        public void AddDirectory(string virtualPath)
        {
            EnsureDirectory(virtualPath);
        }

        public void Clear()
        {
            _root.Children.Clear();
            _root.Files.Clear();
            _allFiles.Clear();
        }

        public VfsBuildResult Build()
        {
            // Collect all directories and files in order
            var dirs = new List<VfsDirNode>();
            var files = new List<PackFileItem>();
            CollectNodes(_root, dirs, files);

            // Build string pool
            var stringPool = new StringBuilder();
            var stringOffsets = new Dictionary<string, uint>();

            uint GetStringOffset(string s)
            {
                if (stringOffsets.TryGetValue(s, out var off))
                    return off;
                off = (uint)stringPool.Length;
                stringOffsets[s] = off;
                stringPool.Append(s);
                stringPool.Append('\0');
                return off;
            }

            var dirIndexMap = new Dictionary<VfsDirNode, int>();
            for (int i = 0; i < dirs.Count; i++)
                dirIndexMap[dirs[i]] = i;

            // Serialize directory entries
            var dirEntries = new VfsDirEntry[dirs.Count];
            for (int i = 0; i < dirs.Count; i++)
            {
                var dir = dirs[i];
                dirEntries[i] = new VfsDirEntry
                {
                    NameOffset = GetStringOffset(dir.Name),
                    ParentIndex = dir.Parent != null ? (uint)dirIndexMap[dir.Parent] : VfsDirEntry.INVALID_INDEX,
                    FirstChild = VfsDirEntry.INVALID_INDEX,
                    NextSibling = VfsDirEntry.INVALID_INDEX,
                    FirstFile = VfsDirEntry.INVALID_INDEX
                };
            }

            // Set child/sibling indices for directories
            for (int i = 0; i < dirs.Count; i++)
            {
                var dir = dirs[i];
                if (dir.Children.Count > 0)
                {
                    dirEntries[i].FirstChild = (uint)dirIndexMap[dir.Children[0]];
                    for (int j = 0; j < dir.Children.Count - 1; j++)
                    {
                        var childIdx = dirIndexMap[dir.Children[j]];
                        var nextIdx = dirIndexMap[dir.Children[j + 1]];
                        dirEntries[childIdx].NextSibling = (uint)nextIdx;
                    }
                }
            }

            // Build data region and file entries
            var dataStream = new MemoryStream();
            var fileEntries = new VfsFileEntry[files.Count];
            long totalOriginalSize = 0;
            long totalCompressedSize = 0;

            for (int i = 0; i < files.Count; i++)
            {
                var file = files[i];
                var fileData = File.ReadAllBytes(file.SourcePath);
                totalOriginalSize += fileData.Length;

                byte[] storeData;
                uint storeSize;
                byte isCompressed;

                if (file.IsCompressed && fileData.Length > 0)
                {
                    var compressed = _compressor.Compress(fileData);
                    if (compressed.Length < fileData.Length)
                    {
                        storeData = compressed;
                        storeSize = (uint)compressed.Length;
                        isCompressed = 1;
                    }
                    else
                    {
                        storeData = fileData;
                        storeSize = (uint)fileData.Length;
                        isCompressed = 0;
                    }
                }
                else
                {
                    storeData = fileData;
                    storeSize = (uint)fileData.Length;
                    isCompressed = 0;
                }

                totalCompressedSize += storeSize;

                var dataOffset = (uint)dataStream.Position;
                dataStream.Write(storeData, 0, storeData.Length);

                // Find directory index
                var fileDir = Path.GetDirectoryName(file.VirtualPath) ?? "";
                uint dirIndex = 0;
                for (int d = 0; d < dirs.Count; d++)
                {
                    if (GetFullPath(dirs[d]) == fileDir)
                    {
                        dirIndex = (uint)d;
                        break;
                    }
                }

                fileEntries[i] = new VfsFileEntry
                {
                    NameOffset = GetStringOffset(Path.GetFileName(file.VirtualPath)),
                    DirIndex = dirIndex,
                    DataOffset = dataOffset,
                    DataSize = storeSize,
                    OriginalSize = (uint)fileData.Length,
                    Attributes = (uint)file.Attributes,
                    LastWriteTime = (ulong)file.LastWriteTime.ToFileTime(),
                    IsCompressed = isCompressed,
                    IsVirtualized = file.IsVirtualized ? (byte)1 : (byte)0
                };
            }

            // Set FirstFile for directories
            for (int d = 0; d < dirs.Count; d++)
            {
                if (dirs[d].Files.Count > 0)
                {
                    dirEntries[d].FirstFile = (uint)files.IndexOf(dirs[d].Files[0]);
                }
            }

            // Serialize metadata
            var metadataStream = new MemoryStream();
            using (var writer = new BinaryWriter(metadataStream, Encoding.UTF8, leaveOpen: true))
            {
                // Placeholder for VFS_HEADER (will be filled later)
                var headerPos = metadataStream.Position;
                var header = new VfsHeader
                {
                    Magic = VfsHeader.MAGIC,
                    Version = VfsHeader.CURRENT_VERSION,
                    FileCount = (uint)files.Count,
                    DirCount = (uint)dirs.Count
                };
                header.WriteTo(writer);

                // Write directory entries
                foreach (var de in dirEntries)
                    de.WriteTo(writer);

                // Write file entries
                foreach (var fe in fileEntries)
                    fe.WriteTo(writer);

                // Write string pool
                var poolBytes = Encoding.UTF8.GetBytes(stringPool.ToString());
                writer.Write(poolBytes);

                // Now fill in offsets
                header.MetadataOffset = (uint)headerPos;
                header.MetadataSize = (uint)metadataStream.Length;
                header.DataOffset = 0; // Data region starts at offset 0 in its own buffer
                header.DataSize = (uint)dataStream.Length;

                // Compute CRC32 over metadata (excluding checksum field) + data
                var metadataBytes = new byte[metadataStream.Length];
                metadataStream.Position = 0;
                metadataStream.Read(metadataBytes, 0, metadataBytes.Length);

                // CRC32 over all metadata + data
                var crcData = new byte[metadataBytes.Length + dataStream.Length];
                Array.Copy(metadataBytes, crcData, metadataBytes.Length);
                Array.Copy(dataStream.ToArray(), 0, crcData, metadataBytes.Length, dataStream.Length);
                header.Checksum = Crc32.Compute(crcData);

                // Rewrite header with correct values
                metadataStream.Position = headerPos;
                header.WriteTo(writer);
            }

            var finalMetadata = new byte[metadataStream.Length];
            metadataStream.Position = 0;
            metadataStream.Read(finalMetadata, 0, finalMetadata.Length);

            return new VfsBuildResult
            {
                Metadata = finalMetadata,
                DataRegion = dataStream.ToArray(),
                FileCount = files.Count,
                DirCount = dirs.Count,
                TotalOriginalSize = totalOriginalSize,
                TotalCompressedSize = totalCompressedSize
            };
        }

        private VfsDirNode EnsureDirectory(string virtualPath)
        {
            if (string.IsNullOrEmpty(virtualPath))
                return _root;

            var parts = virtualPath.Split(new[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries);
            var current = _root;

            foreach (var part in parts)
            {
                var child = current.Children.Find(c => c.Name == part);
                if (child == null)
                {
                    child = new VfsDirNode { Name = part, Parent = current };
                    current.Children.Add(child);
                }
                current = child;
            }

            return current;
        }

        private static void CollectNodes(VfsDirNode node, List<VfsDirNode> dirs, List<PackFileItem> files)
        {
            if (node == null) return;

            if (node.Name != "")
                dirs.Add(node);

            foreach (var file in node.Files)
                files.Add(file);

            foreach (var child in node.Children)
                CollectNodes(child, dirs, files);
        }

        private static string GetFullPath(VfsDirNode node)
        {
            if (node.Parent == null || string.IsNullOrEmpty(node.Parent.Name))
                return node.Name;
            return GetFullPath(node.Parent) + "\\" + node.Name;
        }
    }
}
