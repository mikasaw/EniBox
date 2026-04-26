using System;
using System.IO;
using System.Runtime.InteropServices;

namespace EniBox.GUI.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct VfsFileEntry
    {
        public uint NameOffset;
        public uint DirIndex;
        public uint DataOffset;
        public uint DataSize;
        public uint OriginalSize;
        public uint Attributes;
        public ulong LastWriteTime;
        public byte IsCompressed;
        public byte IsVirtualized;
        public byte Reserved0;
        public byte Reserved1;

        public void WriteTo(BinaryWriter writer)
        {
            writer.Write(NameOffset);
            writer.Write(DirIndex);
            writer.Write(DataOffset);
            writer.Write(DataSize);
            writer.Write(OriginalSize);
            writer.Write(Attributes);
            writer.Write(LastWriteTime);
            writer.Write(IsCompressed);
            writer.Write(IsVirtualized);
            writer.Write(Reserved0);
            writer.Write(Reserved1);
        }

        public static VfsFileEntry ReadFrom(BinaryReader reader)
        {
            return new VfsFileEntry
            {
                NameOffset = reader.ReadUInt32(),
                DirIndex = reader.ReadUInt32(),
                DataOffset = reader.ReadUInt32(),
                DataSize = reader.ReadUInt32(),
                OriginalSize = reader.ReadUInt32(),
                Attributes = reader.ReadUInt32(),
                LastWriteTime = reader.ReadUInt64(),
                IsCompressed = reader.ReadByte(),
                IsVirtualized = reader.ReadByte(),
                Reserved0 = reader.ReadByte(),
                Reserved1 = reader.ReadByte()
            };
        }
    }
}
