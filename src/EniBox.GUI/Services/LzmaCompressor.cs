using System;
using SevenZip;
using SevenZip.Compression.LZMA;

namespace EniBox.GUI.Services
{
    public sealed class LzmaCompressor : ICompressor
    {
        public string AlgorithmId => "LZMA";

        public byte[] Compress(ReadOnlySpan<byte> data)
        {
            if (data.Length == 0)
                return Array.Empty<byte>();

            var encoder = new Encoder();
            encoder.SetCoderProperties(
                new CoderPropID[]
                {
                    CoderPropID.DictionarySize,
                    CoderPropID.PosStateBits,
                    CoderPropID.LitContextBits,
                    CoderPropID.LitPosBits,
                    CoderPropID.Algorithm,
                    CoderPropID.NumFastBytes,
                    CoderPropID.MatchFinder,
                    CoderPropID.EndMarker
                },
                new object[]
                {
                    1 << 23,   // DictionarySize: 8MB
                    2,         // PosStateBits
                    3,         // LitContextBits
                    0,         // LitPosBits
                    2,         // Algorithm
                    128,       // NumFastBytes
                    "BT4",     // MatchFinder
                    false      // EndMarker
                }
            );

            var inStream = new System.IO.MemoryStream(data.ToArray());
            var outStream = new System.IO.MemoryStream();

            // Write properties header (5 bytes)
            encoder.WriteCoderProperties(outStream);

            // Write original size (8 bytes, little-endian)
            var sizeBytes = BitConverter.GetBytes((long)data.Length);
            outStream.Write(sizeBytes, 0, 8);

            // Compress data
            encoder.Code(inStream, outStream, data.Length, -1, null);
            return outStream.ToArray();
        }

        public byte[] Decompress(ReadOnlySpan<byte> compressedData, int originalSize)
        {
            if (compressedData.Length == 0)
                return Array.Empty<byte>();

            var inStream = new System.IO.MemoryStream(compressedData.ToArray());
            var outStream = new System.IO.MemoryStream(originalSize);

            var decoder = new Decoder();

            // Read properties header (5 bytes)
            var properties = new byte[5];
            inStream.Read(properties, 0, 5);
            decoder.SetDecoderProperties(properties);

            // Read original size (8 bytes)
            var sizeBytes = new byte[8];
            inStream.Read(sizeBytes, 0, 8);
            long compressedSize = inStream.Length - inStream.Position;

            decoder.Code(inStream, outStream, compressedSize, originalSize, null);
            return outStream.ToArray();
        }
    }
}
