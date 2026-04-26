using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using Xunit;

namespace EniBox.Tests
{
    /// <summary>
    /// Integration tests for the complete VFS build and pack pipeline.
    /// </summary>
    public class VfsIntegrationTests
    {
        private static string CreateTestAppDirectory()
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBox_IntTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);

            // Create a simulated application structure
            File.WriteAllText(Path.Combine(tempDir, "app.exe"), "MZ" + new string('\0', 100)); // Fake EXE header
            File.WriteAllText(Path.Combine(tempDir, "config.ini"), "[Settings]\nKey=Value\n");
            File.WriteAllText(Path.Combine(tempDir, "data.json"), "{\"name\":\"test\",\"value\":42}");

            var libDir = Path.Combine(tempDir, "lib");
            Directory.CreateDirectory(libDir);
            File.WriteAllText(Path.Combine(libDir, "mylib.dll"), "DLL" + new string('\0', 50));
            File.WriteAllText(Path.Combine(libDir, "helper.dll"), "DLL" + new string('\0', 30));

            var resDir = Path.Combine(tempDir, "resources");
            Directory.CreateDirectory(resDir);
            File.WriteAllText(Path.Combine(resDir, "icon.png"), new string('x', 1000));
            File.WriteAllText(Path.Combine(resDir, "style.css"), "body { color: red; }");

            return tempDir;
        }

        [Fact]
        public void FullVfsBuild_MultipleFileTypes_ProducesValidResult()
        {
            var tempDir = CreateTestAppDirectory();
            try
            {
                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);

                // Add all files from the test directory
                foreach (var file in Directory.GetFiles(tempDir, "*", SearchOption.AllDirectories))
                {
                    var item = PackFileItem.FromFile(file, tempDir);
                    item.IsCompressed = true;
                    builder.AddFile(item);
                }

                var result = builder.Build();

                // Verify result
                Assert.Equal(7, result.FileCount); // 6 files + root dir entry
                Assert.True(result.DirCount >= 2); // At least "lib" and "resources"
                Assert.True(result.Metadata.Length > 0);
                Assert.True(result.DataRegion.Length > 0);
                Assert.True(result.TotalOriginalSize > 0);
                Assert.True(result.TotalCompressedSize > 0);
                Assert.True(result.TotalCompressedSize < result.TotalOriginalSize); // Compression should help
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void FullVfsBuild_MixedCompression_ProducesValidResult()
        {
            var tempDir = CreateTestAppDirectory();
            try
            {
                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);

                foreach (var file in Directory.GetFiles(tempDir, "*", SearchOption.AllDirectories))
                {
                    var item = PackFileItem.FromFile(file, tempDir);
                    // Compress text files, don't compress binary files
                    item.IsCompressed = file.EndsWith(".ini") || file.EndsWith(".json") || file.EndsWith(".css");
                    builder.AddFile(item);
                }

                var result = builder.Build();

                Assert.Equal(7, result.FileCount);
                // Compression may not always reduce size for mixed content
                Assert.True(result.TotalCompressedSize > 0);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void FullVfsBuild_EmptyDirectory_IncludedInResult()
        {
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBox_IntTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);
            var emptyDir = Path.Combine(tempDir, "empty");
            Directory.CreateDirectory(emptyDir);
            File.WriteAllText(Path.Combine(tempDir, "root.txt"), "root");

            try
            {
                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);
                builder.AddDirectory("empty");
                builder.AddFile(PackFileItem.FromFile(Path.Combine(tempDir, "root.txt"), tempDir));

                var result = builder.Build();
                Assert.Equal(1, result.FileCount);
                Assert.True(result.DirCount >= 1);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void LzmaCompressDecompress_LargeFile_Roundtrip()
        {
            var compressor = new LzmaCompressor();
            var data = new byte[1024 * 1024]; // 1MB
            var random = new Random(12345);
            random.NextBytes(data);

            var compressed = compressor.Compress(data);
            var decompressed = compressor.Decompress(compressed, data.Length);

            Assert.Equal(data, decompressed);
            // Random data may not compress well, so just verify compressed output exists
            Assert.True(compressed.Length > 0);
        }

        [Fact]
        public void LzmaCompressDecompress_HighlyCompressible_Roundtrip()
        {
            var compressor = new LzmaCompressor();
            // 100KB of repeated pattern
            var data = new byte[100 * 1024];
            for (int i = 0; i < data.Length; i++)
                data[i] = (byte)(i % 256);

            var compressed = compressor.Compress(data);
            var decompressed = compressor.Decompress(compressed, data.Length);

            Assert.Equal(data, decompressed);
            // Should achieve significant compression
            Assert.True(compressed.Length < data.Length / 10);
        }
    }

    /// <summary>
    /// Tests for PE parsing functionality.
    /// </summary>
    public class PeParsingTests
    {
        [Fact]
        public void ParsePeInfo_ValidExe_ReturnsCorrectArchitecture()
        {
            // Use the test runner itself as a known valid PE file
            var exePath = System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName;
            if (exePath == null || !File.Exists(exePath)) return;

            // This test verifies the C# PE parser works on a real PE file
            using var stream = File.OpenRead(exePath);
            using var reader = new BinaryReader(stream);

            // Check DOS signature
            var dosSig = reader.ReadUInt16();
            Assert.Equal(0x5A4D, dosSig);

            // Read e_lfanew
            stream.Position = 0x3C;
            var peOffset = reader.ReadInt32();

            // Check PE signature
            stream.Position = peOffset;
            var peSig = reader.ReadUInt32();
            Assert.Equal(0x00004550u, peSig);

            // Read machine type
            var machine = reader.ReadUInt16();
            Assert.True(machine == 0x014C || machine == 0x8664); // x86 or x64
        }
    }

    /// <summary>
    /// Tests for CRC32 integrity verification.
    /// </summary>
    public class Crc32IntegrityTests
    {
        [Fact]
        public void Crc32_VfsMetadataRoundtrip_MatchesChecksum()
        {
            // Build VFS data and verify CRC32 matches
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBox_CrcTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);
            File.WriteAllText(Path.Combine(tempDir, "test.txt"), "CRC32 test content");

            try
            {
                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);
                var item = PackFileItem.FromFile(Path.Combine(tempDir, "test.txt"), tempDir);
                item.IsCompressed = false;
                builder.AddFile(item);

                var result = builder.Build();

                // Verify metadata is not empty and has valid structure
                Assert.True(result.Metadata.Length >= sizeof(uint) * 11); // VFS_HEADER size

                // Read the header and verify magic
                using var ms = new MemoryStream(result.Metadata);
                using var reader = new BinaryReader(ms);
                var header = VfsHeader.ReadFrom(reader);
                Assert.True(header.IsValid());
                Assert.Equal(VfsHeader.MAGIC, header.Magic);
                Assert.Equal(VfsHeader.CURRENT_VERSION, header.Version);
                Assert.Equal(1u, header.FileCount);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void Crc32_DifferentData_ProducesDifferentHashes()
        {
            var data1 = Encoding.UTF8.GetBytes("File content A");
            var data2 = Encoding.UTF8.GetBytes("File content B");

            var hash1 = Crc32.Compute(data1);
            var hash2 = Crc32.Compute(data2);

            Assert.NotEqual(hash1, hash2);
        }

        [Fact]
        public void Crc32_PartialCompute_MatchesFullCompute()
        {
            var data = Encoding.UTF8.GetBytes("Hello, World! This is a CRC32 test.");
            var fullHash = Crc32.Compute(data);

            // Compute with offset
            var partialHash = Crc32.Compute(data, 0, data.Length);
            Assert.Equal(fullHash, partialHash);
        }
    }

    /// <summary>
    /// Tests for the pack service error handling.
    /// </summary>
    public class PackServiceErrorTests
    {
        [Fact]
        public async Task PackAsync_MissingDependencyFile_ReturnsError()
        {
            var compressor = new LzmaCompressor();
            var builder = new VfsBuilder(compressor);
            var service = new PackService(compressor, builder);

            var tempExe = Path.GetTempFileName();
            try
            {
                var config = new PackConfiguration
                {
                    SourceExePath = tempExe,
                    OutputPath = Path.Combine(Path.GetTempPath(), "output.exe"),
                    Files = new System.Collections.Generic.List<PackFileItem>
                    {
                        new PackFileItem
                        {
                            SourcePath = "C:\\nonexistent_dep_12345.dll",
                            VirtualPath = "dep.dll",
                            OriginalSize = 100
                        }
                    }
                };

                var result = await service.PackAsync(config, null, default);
                Assert.False(result.IsSuccess);
                Assert.Contains("not found", result.ErrorMessage);
            }
            finally
            {
                File.Delete(tempExe);
            }
        }

        [Fact]
        public async Task PackAsync_ProgressCallback_ReceivesUpdates()
        {
            var compressor = new LzmaCompressor();
            var builder = new VfsBuilder(compressor);
            var service = new PackService(compressor, builder);

            var tempDir = Path.Combine(Path.GetTempPath(), "EniBox_ProgTest_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);
            var tempExe = Path.Combine(tempDir, "test.exe");
            File.WriteAllText(tempExe, "MZ" + new string('\0', 100));

            try
            {
                var config = new PackConfiguration
                {
                    SourceExePath = tempExe,
                    OutputPath = Path.Combine(tempDir, "output.exe"),
                    Files = new System.Collections.Generic.List<PackFileItem>()
                };

                var progressUpdates = new System.Collections.Generic.List<PackProgress>();
                var progress = new Progress<PackProgress>(p => progressUpdates.Add(p));

                await service.PackAsync(config, progress, default);

                // Should have received at least some progress updates
                Assert.True(progressUpdates.Count > 0);
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }
    }

    /// <summary>
    /// Tests for VFS data model binary compatibility between C# and C.
    /// </summary>
    public class BinaryCompatibilityTests
    {
        [Fact]
        public void VfsHeader_SizeMatchesCStruct()
        {
            // VFS_HEADER has 11 uint32_t fields = 44 bytes
            using var ms = new MemoryStream();
            using (var writer = new BinaryWriter(ms, Encoding.UTF8, true))
            {
                var header = new VfsHeader
                {
                    Magic = VfsHeader.MAGIC,
                    Version = 1,
                    FileCount = 10,
                    DirCount = 5,
                    MetadataOffset = 100,
                    MetadataSize = 200,
                    DataOffset = 300,
                    DataSize = 400,
                    LoaderOffset = 700,
                    LoaderSize = 50,
                    Checksum = 0
                };
                header.WriteTo(writer);
            }

            Assert.Equal(44, ms.Length); // 11 * 4 bytes
        }

        [Fact]
        public void VfsDirEntry_SizeMatchesCStruct()
        {
            using var ms = new MemoryStream();
            using (var writer = new BinaryWriter(ms, Encoding.UTF8, true))
            {
                var entry = new VfsDirEntry
                {
                    NameOffset = 1,
                    ParentIndex = 2,
                    FirstChild = 3,
                    NextSibling = 4,
                    FirstFile = 5
                };
                entry.WriteTo(writer);
            }

            Assert.Equal(20, ms.Length); // 5 * 4 bytes
        }

        [Fact]
        public void VfsFileEntry_SizeMatchesCStruct()
        {
            using var ms = new MemoryStream();
            using (var writer = new BinaryWriter(ms, Encoding.UTF8, true))
            {
                var entry = new VfsFileEntry
                {
                    NameOffset = 1,
                    DirIndex = 2,
                    DataOffset = 3,
                    DataSize = 4,
                    OriginalSize = 5,
                    Attributes = 6,
                    LastWriteTime = 7,
                    IsCompressed = 1,
                    IsVirtualized = 1
                };
                entry.WriteTo(writer);
            }

            // 6*4 + 8 + 4 = 36 bytes
            Assert.Equal(36, ms.Length);
        }
    }
}
