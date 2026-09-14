using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public sealed class VfsBuilder : IVfsBuilder
    {
        private const int LargeFileThreshold = 64 * 1024;
        private const int StreamBufferSize = 64 * 1024;
        private class VfsDirNode
        {
            public string Name { get; set; } = string.Empty;
            public VfsDirNode? Parent { get; set; }
            public List<VfsDirNode> Children { get; } = new();
            public List<PackFileItem> Files { get; } = new();

            private string? _fullPath;
            public string FullPath
            {
                get
                {
                    if (_fullPath == null)
                    {
                        if (Parent == null || string.IsNullOrEmpty(Parent.Name))
                            _fullPath = Name;
                        else
                            _fullPath = Parent.FullPath + "/" + Name;
                    }
                    return _fullPath;
                }
            }

            private static string NormalizePath(string path)
            {
                return path.Replace('\\', '/');
            }

            public override bool Equals(object? obj)
            {
                if (obj is not VfsDirNode other) return false;
                if (ReferenceEquals(this, other)) return true;
                return string.Equals(NormalizePath(FullPath), NormalizePath(other.FullPath), StringComparison.OrdinalIgnoreCase);
            }

            public override int GetHashCode()
            {
                return StringComparer.OrdinalIgnoreCase.GetHashCode(NormalizePath(FullPath));
            }

            internal void InvalidatePathCache()
            {
                _fullPath = null;
                foreach (var child in Children)
                    child.InvalidatePathCache();
            }
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
                    ParentIndex = dir.Parent != null && dirIndexMap.TryGetValue(dir.Parent, out var parentIdx) ? (uint)parentIdx : VfsDirEntry.INVALID_INDEX,
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
                var fileInfo = new FileInfo(file.SourcePath);
                var fileSize = fileInfo.Length;
                totalOriginalSize += fileSize;

                byte[] storeData;
                uint storeSize;
                byte isCompressed;

                if (file.IsCompressed && fileSize > 0)
                {
                    byte[] compressed;
                    if (fileSize > LargeFileThreshold)
                    {
                        using var fileStream = new FileStream(file.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, StreamBufferSize, FileOptions.SequentialScan);
                        compressed = _compressor.CompressStream(fileStream, fileSize);
                    }
                    else
                    {
                        var fileData = File.ReadAllBytes(file.SourcePath);
                        compressed = _compressor.Compress(fileData);
                    }

                    if (compressed.Length < fileSize)
                    {
                        storeData = compressed;
                        storeSize = (uint)compressed.Length;
                        isCompressed = 1;
                    }
                    else
                    {
                        storeData = fileSize > LargeFileThreshold ? File.ReadAllBytes(file.SourcePath) : File.ReadAllBytes(file.SourcePath);
                        storeSize = (uint)storeData.Length;
                        isCompressed = 0;
                    }
                }
                else
                {
                    if (fileSize > LargeFileThreshold)
                    {
                        var dataOffset = (uint)dataStream.Position;
                        using (var srcStream = new FileStream(file.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, StreamBufferSize, FileOptions.SequentialScan))
                        {
                            srcStream.CopyTo(dataStream);
                        }
                        storeData = Array.Empty<byte>();
                        storeSize = (uint)fileSize;
                    }
                    else
                    {
                        storeData = File.ReadAllBytes(file.SourcePath);
                        storeSize = (uint)storeData.Length;
                    }
                    isCompressed = 0;
                }

                totalCompressedSize += storeSize;

                uint entryDataOffset;
                if (storeData.Length > 0)
                {
                    entryDataOffset = (uint)dataStream.Position;
                    dataStream.Write(storeData, 0, storeData.Length);
                }
                else
                {
                    entryDataOffset = (uint)(dataStream.Position - storeSize);
                }

                var fileDir = Path.GetDirectoryName(file.VirtualPath) ?? "";
                // VFS_INVALID_INDEX: the C runtime (vfs_hashtable.c) treats this
                // sentinel as "no parent directory". Defaulting to 0 would make
                // it chase directory #0 (which may not exist) and the hash table
                // would end up with a bare file name instead of the full path.
                uint dirIndex = 0xFFFFFFFFu;
                for (int d = 0; d < dirs.Count; d++)
                {
                    if (GetFullPath(dirs[d]) == fileDir)
                    {
                        dirIndex = (uint)d;
                        break;
                    }
                }

                // Callers may leave LastWriteTime unset (default = year 0001, outside
                // the Win32 FILETIME range) — fall back to the source file's timestamp.
                var lastWrite = file.LastWriteTime;
                if (lastWrite == default)
                    lastWrite = File.GetLastWriteTime(file.SourcePath);

                fileEntries[i] = new VfsFileEntry
                {
                    NameOffset = GetStringOffset(Path.GetFileName(file.VirtualPath)),
                    DirIndex = dirIndex,
                    DataOffset = entryDataOffset,
                    DataSize = storeSize,
                    OriginalSize = (uint)fileSize,
                    Attributes = (uint)file.Attributes,
                    LastWriteTime = (ulong)lastWrite.ToFileTime(),
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
            VfsHeader header;
            long headerPos;
            using (var writer = new BinaryWriter(metadataStream, Encoding.UTF8, leaveOpen: true))
            {
                headerPos = metadataStream.Position;
                header = new VfsHeader
                {
                    Magic = VfsHeader.MAGIC,
                    Version = VfsHeader.CURRENT_VERSION,
                    FileCount = (uint)files.Count,
                    DirCount = (uint)dirs.Count
                };
                header.WriteTo(writer);

                foreach (var de in dirEntries)
                    de.WriteTo(writer);

                foreach (var fe in fileEntries)
                    fe.WriteTo(writer);

                var poolBytes = Encoding.UTF8.GetBytes(stringPool.ToString());
                writer.Write(poolBytes);

                header.MetadataOffset = (uint)headerPos;
                header.MetadataSize = (uint)metadataStream.Length;
                header.DataOffset = 0;
                header.DataSize = (uint)dataStream.Length;

                // Rewrite the header so metadataBytes carries the real offset/
                // size fields (checksum still 0): the Loader verifies the CRC
                // over exactly these final bytes.
                metadataStream.Position = headerPos;
                header.WriteTo(writer);
            }

            var metadataBytes = new byte[metadataStream.Length];
            metadataStream.Position = 0;
            metadataStream.Read(metadataBytes, 0, metadataBytes.Length);

            var dataRegion = dataStream.ToArray();

            uint crc = Crc32.StartPartial();
            crc = Crc32.ContinueCompute(crc, metadataBytes);
            crc = Crc32.ContinueCompute(crc, dataRegion);
            header.Checksum = Crc32.FinishPartial(crc);

            using (var writer = new BinaryWriter(metadataStream, Encoding.UTF8, leaveOpen: true))
            {
                metadataStream.Position = headerPos;
                header.WriteTo(writer);
            }

            var finalMetadata = new byte[metadataStream.Length];
            metadataStream.Position = 0;
            metadataStream.Read(finalMetadata, 0, finalMetadata.Length);

            return new VfsBuildResult
            {
                Metadata = finalMetadata,
                DataRegion = dataRegion,
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
