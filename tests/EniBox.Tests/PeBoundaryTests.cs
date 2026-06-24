using System;
using System.IO;
using System.Threading;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using Xunit;

namespace EniBox.Tests
{
    public class PeBoundaryTests
    {
        private static string WriteTempPe(byte[] data)
        {
            var path = Path.Combine(Path.GetTempPath(), $"PeBoundaryTest_{Guid.NewGuid():N}.bin");
            File.WriteAllBytes(path, data);
            return path;
        }

        private static PackService CreatePackService() =>
            new PackService(new LzmaCompressor(), new VfsBuilder(new LzmaCompressor()));

        private static PackResult PackFile(PackService svc, string sourcePath, string outputPath)
        {
            var config = new PackConfiguration { SourceExePath = sourcePath, OutputPath = outputPath };
            return svc.Pack(config, null, CancellationToken.None);
        }

        private static byte[] BuildMinimalPe64()
        {
            var pe = new byte[512];
            pe[0] = 0x4D; pe[1] = 0x5A;
            pe[60] = 0x80;
            int peOff = 0x80;
            pe[peOff] = 0x50; pe[peOff + 1] = 0x45; pe[peOff + 2] = 0x00; pe[peOff + 3] = 0x00;
            pe[peOff + 4] = 0x64; pe[peOff + 5] = 0x86;
            pe[peOff + 20] = 0xF0;
            pe[peOff + 24] = 0x0B; pe[peOff + 25] = 0x02;
            return pe;
        }

        [Fact]
        public void PackService_TruncatedPeHeader_ReturnsInvalidPe()
        {
            var data = new byte[64];
            data[0] = 0x4D; data[1] = 0x5A;
            data[60] = 0x40; data[61] = 0x00; data[62] = 0x00; data[63] = 0x00;
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
                Assert.True(result.ErrorCode == PackErrorCode.InvalidPe || result.ErrorCode == PackErrorCode.ReadFailed,
                    $"Expected InvalidPe or ReadFailed, got {result.ErrorCode}");
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_CorruptedDosMagic_ReturnsInvalidPe()
        {
            var data = BuildMinimalPe64();
            data[0] = 0x00; data[1] = 0x00;
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
                Assert.True(result.ErrorCode == PackErrorCode.InvalidPe || result.ErrorCode == PackErrorCode.ReadFailed);
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_CorruptedPeSignature_ReturnsInvalidPe()
        {
            var data = BuildMinimalPe64();
            int peOff = 0x80;
            data[peOff] = 0x00; data[peOff + 1] = 0x00;
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
                Assert.True(result.ErrorCode == PackErrorCode.InvalidPe || result.ErrorCode == PackErrorCode.ReadFailed);
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_EmptyFile_ReturnsError()
        {
            var path = WriteTempPe(Array.Empty<byte>());
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_RandomGarbage_ReturnsInvalidPe()
        {
            var rng = new Random(42);
            var data = new byte[1024];
            rng.NextBytes(data);
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
                Assert.True(result.ErrorCode == PackErrorCode.InvalidPe || result.ErrorCode == PackErrorCode.ReadFailed);
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_PeOffsetBeyondFile_ReturnsInvalidPe()
        {
            var data = new byte[128];
            data[0] = 0x4D; data[1] = 0x5A;
            data[60] = 0xFF; data[61] = 0xFF; data[62] = 0xFF; data[63] = 0x7F;
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_PeWithZeroOptionalHeader_ReturnsInvalidPe()
        {
            var data = BuildMinimalPe64();
            int peOff = 0x80;
            data[peOff + 20] = 0x00; data[peOff + 21] = 0x00;
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public void PackService_PeWithOversizedSectionCount_ReturnsError()
        {
            var data = BuildMinimalPe64();
            int peOff = 0x80;
            data[peOff + 6] = 0xFF; data[peOff + 7] = 0xFF;
            var path = WriteTempPe(data);
            try
            {
                var result = PackFile(CreatePackService(), path, path + ".out");
                Assert.False(result.IsSuccess);
            }
            finally { File.Delete(path); }
        }
    }
}
