using System;
using System.IO;

namespace EniBox.GUI.Services
{
    public interface ICompressor
    {
        byte[] Compress(ReadOnlySpan<byte> data);
        byte[] Decompress(ReadOnlySpan<byte> compressedData, int originalSize);
        string AlgorithmId { get; }

        byte[] CompressArray(byte[] data) => Compress(data);

        byte[] DecompressArray(byte[] compressedData, int originalSize) => Decompress(compressedData, originalSize);

        byte[] CompressStream(Stream input, long inputLength)
        {
            if (inputLength <= 0)
                throw new ArgumentOutOfRangeException(nameof(inputLength));

            byte[] buffer = new byte[inputLength];
            input.ReadExactly(buffer, 0, (int)inputLength);
            return CompressArray(buffer);
        }
    }
}
