using System;
using System.IO;
using SevenZip;
using SevenZip.Compression.LZMA;

namespace EniBox.GUI.Services
{
    public sealed class LzmaCompressor : ICompressor
    {
        private const int DictionarySize = 1 << 23;
        private const int PosStateBits = 2;
        private const int LitContextBits = 3;
        private const int NumFastBytes = 128;

        public string AlgorithmId => "LZMA";

        public byte[] Compress(ReadOnlySpan<byte> data)
        {
            if (data.Length == 0)
                return Array.Empty<byte>();

            var encoder = CreateEncoder();

            byte[] buffer = data.ToArray();
            var inStream = new MemoryStream(buffer, 0, buffer.Length, false);
            var outStream = new MemoryStream();

            encoder.WriteCoderProperties(outStream);

            var sizeBytes = BitConverter.GetBytes((long)data.Length);
            outStream.Write(sizeBytes, 0, 8);

            encoder.Code(inStream, outStream, data.Length, -1, null);
            return outStream.ToArray();
        }

        public byte[] CompressArray(byte[] data)
        {
            if (data.Length == 0)
                return Array.Empty<byte>();

            var encoder = CreateEncoder();

            var inStream = new MemoryStream(data, 0, data.Length, false);
            var outStream = new MemoryStream();

            encoder.WriteCoderProperties(outStream);

            var sizeBytes = BitConverter.GetBytes((long)data.Length);
            outStream.Write(sizeBytes, 0, 8);

            encoder.Code(inStream, outStream, data.Length, -1, null);
            return outStream.ToArray();
        }

        public byte[] Decompress(ReadOnlySpan<byte> compressedData, int originalSize)
        {
            if (compressedData.Length == 0)
                return Array.Empty<byte>();

            byte[] buffer = compressedData.ToArray();
            var inStream = new MemoryStream(buffer, 0, buffer.Length, false);
            var outStream = new MemoryStream(originalSize);

            var decoder = new Decoder();

            var properties = new byte[5];
            inStream.Read(properties, 0, 5);
            decoder.SetDecoderProperties(properties);

            var sizeBytes = new byte[8];
            inStream.Read(sizeBytes, 0, 8);
            long compressedSize = inStream.Length - inStream.Position;

            decoder.Code(inStream, outStream, compressedSize, originalSize, null);
            return outStream.ToArray();
        }

        public byte[] DecompressArray(byte[] compressedData, int originalSize)
        {
            if (compressedData.Length == 0)
                return Array.Empty<byte>();

            var inStream = new MemoryStream(compressedData, 0, compressedData.Length, false);
            var outStream = new MemoryStream(originalSize);

            var decoder = new Decoder();

            var properties = new byte[5];
            inStream.Read(properties, 0, 5);
            decoder.SetDecoderProperties(properties);

            var sizeBytes = new byte[8];
            inStream.Read(sizeBytes, 0, 8);
            long compressedSize = inStream.Length - inStream.Position;

            decoder.Code(inStream, outStream, compressedSize, originalSize, null);
            return outStream.ToArray();
        }

        public byte[] CompressStream(Stream input, long inputLength)
        {
            if (inputLength == 0)
                return Array.Empty<byte>();

            var encoder = CreateEncoder();
            var outStream = new MemoryStream();

            encoder.WriteCoderProperties(outStream);

            var sizeBytes = BitConverter.GetBytes(inputLength);
            outStream.Write(sizeBytes, 0, 8);

            encoder.Code(input, outStream, inputLength, -1, null);
            return outStream.ToArray();
        }

        private static Encoder CreateEncoder()
        {
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
                    DictionarySize,
                    PosStateBits,
                    LitContextBits,
                    0,
                    2,
                    NumFastBytes,
                    "BT4",
                    false
                }
            );
            return encoder;
        }
    }
}
