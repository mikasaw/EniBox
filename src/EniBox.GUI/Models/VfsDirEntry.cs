using System;
using System.IO;
using System.Runtime.InteropServices;

namespace EniBox.GUI.Models
{
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct VfsDirEntry
    {
        public const uint INVALID_INDEX = 0xFFFFFFFF;

        public uint NameOffset;
        public uint ParentIndex;
        public uint FirstChild;
        public uint NextSibling;
        public uint FirstFile;

        public void WriteTo(BinaryWriter writer)
        {
            writer.Write(NameOffset);
            writer.Write(ParentIndex);
            writer.Write(FirstChild);
            writer.Write(NextSibling);
            writer.Write(FirstFile);
        }

        public static VfsDirEntry ReadFrom(BinaryReader reader)
        {
            return new VfsDirEntry
            {
                NameOffset = reader.ReadUInt32(),
                ParentIndex = reader.ReadUInt32(),
                FirstChild = reader.ReadUInt32(),
                NextSibling = reader.ReadUInt32(),
                FirstFile = reader.ReadUInt32()
            };
        }
    }
}
