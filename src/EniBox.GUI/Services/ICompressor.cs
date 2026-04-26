using System;

namespace EniBox.GUI.Services
{
    public interface ICompressor
    {
        byte[] Compress(ReadOnlySpan<byte> data);
        byte[] Decompress(ReadOnlySpan<byte> compressedData, int originalSize);
        string AlgorithmId { get; }
    }
}
