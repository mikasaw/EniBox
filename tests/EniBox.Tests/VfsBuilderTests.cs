using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using Xunit;

namespace EniBox.Tests
{
    public class VfsBuilderTests
    {
        private static string CreateTempFile(string content, string? subDir = null)
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBoxTest_" + Guid.NewGuid().ToString("N")[..8]);
            if (subDir != null)
                tempDir = Path.Combine(tempDir, subDir);
            Directory.CreateDirectory(tempDir);
            var filePath = Path.Combine(tempDir, "testfile.txt");
            File.WriteAllText(filePath, content);
            return filePath;
        }

        [Fact]
        public void Build_SingleFile_ProducesValidResult()
        {
            var tempFile = CreateTempFile("Hello, VFS!");
            try
            {
                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);
                var baseDir = Path.GetDirectoryName(tempFile)!;

                var item = PackFileItem.FromFile(tempFile, baseDir);
                item.IsCompressed = false;
                builder.AddFile(item);

                var result = builder.Build();

                Assert.Equal(1, result.FileCount);
                Assert.True(result.Metadata.Length > 0);
                Assert.True(result.DataRegion.Length > 0);
                Assert.True(result.TotalOriginalSize > 0);
            }
            finally
            {
                try { Directory.Delete(Path.GetDirectoryName(Path.GetDirectoryName(tempFile))!, true); } catch { }
            }
        }

        [Fact]
        public void Build_MultipleFiles_ProducesCorrectCount()
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBoxTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);
            try
            {
                File.WriteAllText(Path.Combine(tempDir, "a.txt"), "AAA");
                File.WriteAllText(Path.Combine(tempDir, "b.txt"), "BBB");

                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);

                foreach (var f in Directory.GetFiles(tempDir))
                {
                    var item = PackFileItem.FromFile(f, tempDir);
                    item.IsCompressed = false;
                    builder.AddFile(item);
                }

                var result = builder.Build();
                Assert.Equal(2, result.FileCount);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void Build_NestedDirectories_ProducesCorrectDirCount()
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBoxTest_" + Guid.NewGuid().ToString("N")[..8]);
            var subDir = Path.Combine(tempDir, "sub", "deep");
            Directory.CreateDirectory(subDir);
            try
            {
                File.WriteAllText(Path.Combine(tempDir, "root.txt"), "root");
                File.WriteAllText(Path.Combine(subDir, "deep.txt"), "deep");

                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);

                foreach (var f in Directory.GetFiles(tempDir, "*", SearchOption.AllDirectories))
                {
                    var item = PackFileItem.FromFile(f, tempDir);
                    item.IsCompressed = false;
                    builder.AddFile(item);
                }

                var result = builder.Build();
                Assert.Equal(2, result.FileCount);
                Assert.True(result.DirCount >= 2); // At least "sub" and "deep"
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void Build_CompressedFile_SmallerThanOriginal()
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBoxTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);
            try
            {
                // Create a file with highly compressible content
                var content = new string('A', 10000);
                var filePath = Path.Combine(tempDir, "compressible.txt");
                File.WriteAllText(filePath, content);

                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);

                var item = PackFileItem.FromFile(filePath, tempDir);
                item.IsCompressed = true;
                builder.AddFile(item);

                var result = builder.Build();

                Assert.True(result.TotalCompressedSize < result.TotalOriginalSize);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void Clear_RemovesAllFiles()
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBoxTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);
            try
            {
                File.WriteAllText(Path.Combine(tempDir, "test.txt"), "test");

                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);
                builder.AddFile(PackFileItem.FromFile(Path.Combine(tempDir, "test.txt"), tempDir));

                builder.Clear();

                var result = builder.Build();
                Assert.Equal(0, result.FileCount);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }
    }

    public class LzmaCompressorTests
    {
        [Fact]
        public void AlgorithmId_ReturnsLZMA()
        {
            var compressor = new LzmaCompressor();
            Assert.Equal("LZMA", compressor.AlgorithmId);
        }

        [Fact]
        public void CompressDecompress_Roundtrip_PreservesData()
        {
            var compressor = new LzmaCompressor();
            var original = Encoding.UTF8.GetBytes("Hello, LZMA compression! This is a test string with some repeated data: AAAAABBBBBCCCCC");

            var compressed = compressor.Compress(original);
            var decompressed = compressor.Decompress(compressed, original.Length);

            Assert.Equal(original, decompressed);
        }

        [Fact]
        public void Compress_ProducesSmallerOutput_ForCompressibleData()
        {
            var compressor = new LzmaCompressor();
            var data = new byte[10000];
            for (int i = 0; i < data.Length; i++) data[i] = (byte)(i % 10);

            var compressed = compressor.Compress(data);
            Assert.True(compressed.Length < data.Length);
        }

        [Fact]
        public void CompressDecompress_EmptyData_DoesNotCrash()
        {
            var compressor = new LzmaCompressor();
            var empty = Array.Empty<byte>();

            var compressed = compressor.Compress(empty);
            Assert.Empty(compressed);
        }

        [Fact]
        public void CompressDecompress_SmallData_PreservesData()
        {
            var compressor = new LzmaCompressor();
            var small = new byte[] { 0x01, 0x02, 0x03 };

            var compressed = compressor.Compress(small);
            var decompressed = compressor.Decompress(compressed, small.Length);

            Assert.Equal(small, decompressed);
        }

        [Fact]
        public void CompressDecompress_BinaryData_PreservesData()
        {
            var compressor = new LzmaCompressor();
            var random = new Random(42);
            var data = new byte[4096];
            random.NextBytes(data);

            var compressed = compressor.Compress(data);
            var decompressed = compressor.Decompress(compressed, data.Length);

            Assert.Equal(data, decompressed);
        }
    }

    public class PackServiceValidationTests
    {
        [Fact]
        public async Task PackAsync_EmptySourcePath_ReturnsError()
        {
            var compressor = new LzmaCompressor();
            var builder = new VfsBuilder(compressor);
            var service = new PackService(compressor, builder);

            var config = new PackConfiguration
            {
                SourceExePath = "",
                OutputPath = "output.exe"
            };

            var result = await service.PackAsync(config, null, default);
            Assert.False(result.IsSuccess);
            Assert.Contains("Source EXE", result.ErrorMessage);
        }

        [Fact]
        public async Task PackAsync_NonExistentSource_ReturnsError()
        {
            var compressor = new LzmaCompressor();
            var builder = new VfsBuilder(compressor);
            var service = new PackService(compressor, builder);

            var config = new PackConfiguration
            {
                SourceExePath = "C:\\nonexistent_file_12345.exe",
                OutputPath = "output.exe"
            };

            var result = await service.PackAsync(config, null, default);
            Assert.False(result.IsSuccess);
        }

        [Fact]
        public async Task PackAsync_EmptyOutputPath_ReturnsError()
        {
            var compressor = new LzmaCompressor();
            var builder = new VfsBuilder(compressor);
            var service = new PackService(compressor, builder);

            var config = new PackConfiguration
            {
                SourceExePath = "some.exe",
                OutputPath = ""
            };

            var result = await service.PackAsync(config, null, default);
            Assert.False(result.IsSuccess);
            Assert.NotNull(result.ErrorMessage);
            Assert.True(result.ErrorMessage.Length > 0);
        }
    }
}
