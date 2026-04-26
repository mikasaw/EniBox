namespace EniBox.GUI.Models
{
    public sealed class VfsBuildResult
    {
        public byte[] Metadata { get; init; } = System.Array.Empty<byte>();
        public byte[] DataRegion { get; init; } = System.Array.Empty<byte>();
        public int FileCount { get; init; }
        public int DirCount { get; init; }
        public long TotalOriginalSize { get; init; }
        public long TotalCompressedSize { get; init; }
    }
}
