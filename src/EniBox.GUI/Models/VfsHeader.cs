using System;
using System.IO;
using System.Runtime.InteropServices;

namespace EniBox.GUI.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct VfsHeader
    {
        public const uint MAGIC = 0x42494E45; // 'ENIB'
        public const uint CURRENT_VERSION = 1;

        public uint Magic;
        public uint Version;
        public uint FileCount;
        public uint DirCount;
        public uint MetadataOffset;
        public uint MetadataSize;
        public uint DataOffset;
        public uint DataSize;
        public uint LoaderOffset;
        public uint LoaderSize;
        public uint Checksum;

        public void WriteTo(BinaryWriter writer)
        {
            writer.Write(Magic);
            writer.Write(Version);
            writer.Write(FileCount);
            writer.Write(DirCount);
            writer.Write(MetadataOffset);
            writer.Write(MetadataSize);
            writer.Write(DataOffset);
            writer.Write(DataSize);
            writer.Write(LoaderOffset);
            writer.Write(LoaderSize);
            writer.Write(Checksum);
        }

        public static VfsHeader ReadFrom(BinaryReader reader)
        {
            var header = new VfsHeader
            {
                Magic = reader.ReadUInt32(),
                Version = reader.ReadUInt32(),
                FileCount = reader.ReadUInt32(),
                DirCount = reader.ReadUInt32(),
                MetadataOffset = reader.ReadUInt32(),
                MetadataSize = reader.ReadUInt32(),
                DataOffset = reader.ReadUInt32(),
                DataSize = reader.ReadUInt32(),
                LoaderOffset = reader.ReadUInt32(),
                LoaderSize = reader.ReadUInt32(),
                Checksum = reader.ReadUInt32()
            };
            return header;
        }

        public readonly bool IsValid() => Magic == MAGIC && Version >= 1;
    }
}
