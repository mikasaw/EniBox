using System;
using System.IO;

namespace EniBox.GUI.Models
{
    public sealed class PackFileItem
    {
        public string SourcePath { get; init; } = string.Empty;
        public string VirtualPath { get; set; } = string.Empty;
        public bool IsCompressed { get; set; } = true;
        public bool IsVirtualized { get; set; } = true;
        public long OriginalSize { get; init; }
        public FileAttributes Attributes { get; init; }
        public DateTime LastWriteTime { get; init; }

        public static PackFileItem FromFile(string sourcePath, string baseDir)
        {
            var fi = new FileInfo(sourcePath);
            var relPath = Path.GetRelativePath(baseDir, sourcePath);
            return new PackFileItem
            {
                SourcePath = sourcePath,
                VirtualPath = relPath,
                IsCompressed = true,
                IsVirtualized = true,
                OriginalSize = fi.Length,
                Attributes = fi.Attributes,
                LastWriteTime = fi.LastWriteTime
            };
        }
    }
}
