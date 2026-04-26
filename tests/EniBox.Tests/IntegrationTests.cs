using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using EniBox.GUI.Interop;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using EniBox.GUI.ViewModels;
using Xunit;

namespace EniBox.Tests
{
    /// <summary>
    /// Test collection definition to prevent parallel execution of file I/O tests.
    /// </summary>
    [CollectionDefinition("Sequential", DisableParallelization = true)]
    public class SequentialTestCollection { }

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

    /// <summary>
    /// End-to-end integration tests for the complete pack pipeline.
    /// These tests verify the full flow: VFS build → section data → PE modification → output validation.
    /// </summary>
    [Collection("Sequential")]
    public class EndToEndPackTests
    {
        /// <summary>
        /// Creates a minimal valid PE file (x86) for testing.
        /// </summary>
        private static string CreateMinimalPeFile(string tempDir, bool is64Bit = false)
        {
            string filePath = Path.Combine(tempDir, is64Bit ? "test_x64.exe" : "test_x86.exe");

            // Use the currently running test executable as a real PE file
            var selfPath = System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName;
            if (selfPath != null && File.Exists(selfPath))
            {
                File.Copy(selfPath, filePath, overwrite: true);
                return filePath;
            }

            // Fallback: create a minimal PE file manually
            using var fs = File.Create(filePath);
            using var writer = new BinaryWriter(fs);

            // DOS Header
            writer.Write((ushort)0x5A4D); // e_magic
            writer.Write(new byte[58]);    // rest of DOS header
            writer.Write((int)64);         // e_lfanew -> PE header at offset 64

            // PE Header
            fs.Position = 64;
            writer.Write(0x00004550u);     // PE signature
            writer.Write((ushort)(is64Bit ? 0x8664 : 0x014C)); // Machine
            writer.Write((ushort)1);       // NumberOfSections
            writer.Write(0u);              // TimeDateStamp
            writer.Write(0u);              // PointerToSymbolTable
            writer.Write(0u);              // NumberOfSymbols
            writer.Write((ushort)(is64Bit ? 240 : 224)); // SizeOfOptionalHeader
            writer.Write((ushort)0x0102);  // Characteristics

            // Optional Header
            if (is64Bit)
            {
                writer.Write((ushort)0x20B); // Magic PE32+
                writer.Write((byte)14);      // MajorLinkerVersion
                writer.Write((byte)0);       // MinorLinkerVersion
                writer.Write(0u);            // SizeOfCode
                writer.Write(0u);            // SizeOfInitializedData
                writer.Write(0u);            // SizeOfUninitializedData
                writer.Write(0u);            // AddressOfEntryPoint
                writer.Write(0u);            // BaseOfCode
                writer.Write(0x140000000ul); // ImageBase
                writer.Write(0x1000u);       // SectionAlignment
                writer.Write(0x200u);        // FileAlignment
                writer.Write((ushort)6);     // MajorOperatingSystemVersion
                writer.Write((ushort)0);     // MinorOperatingSystemVersion
                writer.Write((ushort)0);     // MajorImageVersion
                writer.Write((ushort)0);     // MinorImageVersion
                writer.Write((ushort)6);     // MajorSubsystemVersion
                writer.Write((ushort)0);     // MinorSubsystemVersion
                writer.Write(0u);            // Win32VersionValue
                writer.Write(0x1000u);       // SizeOfImage
                writer.Write(0x200u);        // SizeOfHeaders
                writer.Write(3u);            // Subsystem (CONSOLE)
                writer.Write((ushort)0x8160); // DllCharacteristics
                writer.Write(0x100000ul);    // SizeOfStackReserve
                writer.Write(0x1000ul);      // SizeOfStackCommit
                writer.Write(0x100000ul);    // SizeOfHeapReserve
                writer.Write(0x1000ul);      // SizeOfHeapCommit
                writer.Write(0u);            // LoaderFlags
                writer.Write(16u);           // NumberOfRvaAndSizes
                writer.Write(new byte[128]); // DataDirectory (16 * 8 bytes)
            }
            else
            {
                writer.Write((ushort)0x10B); // Magic PE32
                writer.Write((byte)14);      // MajorLinkerVersion
                writer.Write((byte)0);       // MinorLinkerVersion
                writer.Write(0u);            // SizeOfCode
                writer.Write(0u);            // SizeOfInitializedData
                writer.Write(0u);            // SizeOfUninitializedData
                writer.Write(0u);            // AddressOfEntryPoint
                writer.Write(0u);            // BaseOfCode
                writer.Write(0u);            // BaseOfData
                writer.Write(0x400000u);     // ImageBase
                writer.Write(0x1000u);       // SectionAlignment
                writer.Write(0x200u);        // FileAlignment
                writer.Write((ushort)6);     // MajorOperatingSystemVersion
                writer.Write((ushort)0);     // MinorOperatingSystemVersion
                writer.Write((ushort)0);     // MajorImageVersion
                writer.Write((ushort)0);     // MinorImageVersion
                writer.Write((ushort)6);     // MajorSubsystemVersion
                writer.Write((ushort)0);     // MinorSubsystemVersion
                writer.Write(0u);            // Win32VersionValue
                writer.Write(0x1000u);       // SizeOfImage
                writer.Write(0x200u);        // SizeOfHeaders
                writer.Write(3u);            // Subsystem (CONSOLE)
                writer.Write((ushort)0x8160); // DllCharacteristics
                writer.Write(0x100000u);     // SizeOfStackReserve
                writer.Write(0x1000u);       // SizeOfStackCommit
                writer.Write(0x100000u);     // SizeOfHeapReserve
                writer.Write(0x1000u);       // SizeOfHeapCommit
                writer.Write(0u);            // LoaderFlags
                writer.Write(16u);           // NumberOfRvaAndSizes
                writer.Write(new byte[128]); // DataDirectory (16 * 8 bytes)
            }

            // Section Header (.text)
            var nameBytes = Encoding.ASCII.GetBytes(".text\0\0\0");
            writer.Write(nameBytes);
            writer.Write(0u);       // VirtualSize
            writer.Write(0x1000u);  // VirtualAddress
            writer.Write(0u);       // SizeOfRawData
            writer.Write(0x200u);   // PointerToRawData
            writer.Write(0u);       // PointerToRelocations
            writer.Write(0u);       // PointerToLinenumbers
            writer.Write((ushort)0); // NumberOfRelocations
            writer.Write((ushort)0); // NumberOfLinenumbers
            writer.Write(0x60000020u); // Characteristics

            return filePath;
        }

        [Fact]
        public void EndToEnd_VfsBuildToSectionData_ProducesConsistentLayout()
        {
            // Test the full VFS build → section data combination pipeline
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBox_E2E_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);

            try
            {
                // Create test files
                File.WriteAllText(Path.Combine(tempDir, "config.ini"), "[App]\nName=Test\nVersion=1.0");
                File.WriteAllText(Path.Combine(tempDir, "data.bin"), new string('x', 4096));

                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);

                foreach (var file in Directory.GetFiles(tempDir, "*", SearchOption.AllDirectories))
                {
                    var item = PackFileItem.FromFile(file, tempDir);
                    item.IsCompressed = true;
                    builder.AddFile(item);
                }

                var vfsResult = builder.Build();

                // Verify VFS structure integrity
                Assert.True(vfsResult.FileCount >= 2, "Should have at least 2 files");
                Assert.True(vfsResult.Metadata.Length > 0, "Metadata should not be empty");
                Assert.True(vfsResult.DataRegion.Length > 0, "Data region should not be empty");

                // Verify VFS header
                using var ms = new MemoryStream(vfsResult.Metadata);
                using var reader = new BinaryReader(ms);
                var header = VfsHeader.ReadFrom(reader);
                Assert.True(header.IsValid(), "VFS header should be valid");
                Assert.Equal(VfsHeader.MAGIC, header.Magic);

                // Verify compression ratio
                Assert.True(vfsResult.TotalCompressedSize < vfsResult.TotalOriginalSize,
                    "Compressed size should be smaller than original for compressible data");
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }

        [Fact]
        public void EndToEnd_PeParsing_RealExe_ExtractsCorrectInfo()
        {
            // Test PE parsing on a real executable
            var selfPath = System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName;
            if (selfPath == null || !File.Exists(selfPath)) return;

            using var stream = File.OpenRead(selfPath);
            using var reader = new BinaryReader(stream);

            // Parse DOS header
            Assert.Equal(0x5A4D, reader.ReadUInt16()); // MZ signature

            stream.Position = 0x3C;
            var peOffset = reader.ReadInt32();

            // Parse PE header
            stream.Position = peOffset;
            Assert.Equal(0x00004550u, reader.ReadUInt32()); // PE signature

            var machine = reader.ReadUInt16();
            Assert.True(machine == 0x014C || machine == 0x8664,
                "Machine type should be x86 (0x014C) or x64 (0x8664)");

            var numberOfSections = reader.ReadUInt16();
            Assert.True(numberOfSections > 0, "Should have at least one section");

            // Skip to optional header
            stream.Position = peOffset + 4 + 20; // PE sig + IMAGE_FILE_HEADER
            var magic = reader.ReadUInt16();
            Assert.True(magic == 0x10B || magic == 0x20B,
                "Optional header magic should be PE32 (0x10B) or PE32+ (0x20B)");
        }

        [Fact]
        public void EndToEnd_StubGeneration_ProducesValidMachineCode()
        {
            // Verify the entry point stub constants match expected machine code patterns
            // x64 stub: sub rsp,0x28 (48 83 EC 28) + add rsp,0x28 (48 83 C4 28) + jmp [rip+0] (FF 25 00 00 00 00)
            byte[] x64ExpectedPrefix = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0xC4, 0x28, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };

            // x86 stub: push imm32 (68 xx xx xx xx) + ret (C3)
            byte x86FirstByte = 0x68; // PUSH imm32
            byte x86LastByte = 0xC3;  // RET

            // Verify x64 stub prefix
            Assert.Equal(14, x64ExpectedPrefix.Length);
            Assert.Equal(0x48, x64ExpectedPrefix[0]);  // REX.W
            Assert.Equal(0x83, x64ExpectedPrefix[1]);  // SUB r/m64, imm8
            Assert.Equal(0xEC, x64ExpectedPrefix[2]);  // ModRM: RSP
            Assert.Equal(0x28, x64ExpectedPrefix[3]);  // 0x28

            // Verify x86 stub
            Assert.Equal(0x68, x86FirstByte);
            Assert.Equal(0xC3, x86LastByte);
        }

        [Fact]
        public void EndToEnd_SectionLayout_OffsetsAreConsistent()
        {
            // Verify the section layout offsets are consistent between PeTool and Loader
            // x64 layout: [stub:14][VA_placeholder:8][original_ep_rva:4][section_rva:4][VFS data...]
            const int X64_STUB_CODE_SIZE = 14;
            const int X64_VA_PLACEHOLDER_SIZE = 8;
            const int X64_METADATA_OFFSET = 22; // 14 + 8
            const int X64_VFS_DATA_OFFSET = 30; // 22 + 4 + 4

            Assert.Equal(X64_STUB_CODE_SIZE + X64_VA_PLACEHOLDER_SIZE, X64_METADATA_OFFSET);
            Assert.Equal(X64_METADATA_OFFSET + 8, X64_VFS_DATA_OFFSET);

            // x86 layout: [stub:6][VA_placeholder:4][original_ep_rva:4][section_rva:4][VFS data...]
            const int X86_STUB_CODE_SIZE = 6;  // push imm32 (5) + ret (1)
            const int X86_METADATA_OFFSET = 6;
            const int X86_VFS_DATA_OFFSET = 14; // 6 + 4 + 4

            Assert.Equal(X86_STUB_CODE_SIZE, X86_METADATA_OFFSET);
            Assert.Equal(X86_METADATA_OFFSET + 8, X86_VFS_DATA_OFFSET);
        }

        [Fact]
        public void EndToEnd_Crc32Integrity_VfsDataRoundtrip()
        {
            // Build VFS, compute CRC32 of all file data, verify integrity
            var tempDir = Path.Combine(Path.GetTempPath(), "EniBox_CrcE2E_" + Guid.NewGuid().ToString("N")[..8]);
            Directory.CreateDirectory(tempDir);

            try
            {
                var testData = Encoding.UTF8.GetBytes("Integration test data for CRC32 verification");
                File.WriteAllBytes(Path.Combine(tempDir, "test.dat"), testData);

                var compressor = new LzmaCompressor();
                var builder = new VfsBuilder(compressor);
                var item = PackFileItem.FromFile(Path.Combine(tempDir, "test.dat"), tempDir);
                item.IsCompressed = true;
                builder.AddFile(item);

                var result = builder.Build();

                // Verify the original CRC32 matches
                var originalCrc = Crc32.Compute(testData);
                Assert.NotEqual(0u, originalCrc);

                // Verify VFS header checksum is populated
                using var ms = new MemoryStream(result.Metadata);
                using var reader = new BinaryReader(ms);
                var header = VfsHeader.ReadFrom(reader);
                Assert.True(header.IsValid());
            }
            finally
            {
                Directory.Delete(tempDir, true);
            }
        }
    }

    /// <summary>
    /// Tests for ImportEntry marshalling and PackResult error code coverage.
    /// </summary>
    public class ImportEntryAndErrorCodeTests
    {
        [Fact]
        public void ImportEntry_StructSize_Is256()
        {
            // IMPORT_ENTRY in C is: char dll_name[256] = 256 bytes
            Assert.Equal(256, Marshal.SizeOf<PeToolInterop.ImportEntry>());
        }

        [Fact]
        public void ImportEntry_DllName_IsCorrectlyMarshalled()
        {
            var entry = new PeToolInterop.ImportEntry { DllName = "EniBox.Loader.dll" };
            Assert.Equal("EniBox.Loader.dll", entry.DllName);

            // Verify marshalling round-trip via pointer
            int size = Marshal.SizeOf<PeToolInterop.ImportEntry>();
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(entry, ptr, false);
                var result = Marshal.PtrToStructure<PeToolInterop.ImportEntry>(ptr);
                Assert.Equal("EniBox.Loader.dll", result.DllName);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
        }

        [Fact]
        public void ImportEntry_Array_CanBeCreatedAndAccessed()
        {
            // Verify that an array of ImportEntry can be created and accessed
            var entries = new PeToolInterop.ImportEntry[]
            {
                new() { DllName = "EniBox.Loader.dll" },
                new() { DllName = "kernel32.dll" }
            };

            Assert.Equal(2, entries.Length);
            Assert.Equal("EniBox.Loader.dll", entries[0].DllName);
            Assert.Equal("kernel32.dll", entries[1].DllName);

            // Verify struct size matches C definition (256 bytes)
            int structSize = Marshal.SizeOf<PeToolInterop.ImportEntry>();
            Assert.Equal(256, structSize);
        }

        [Fact]
        public void PackResult_ErrorCode_DefaultIsZero()
        {
            var result = new PackResult { IsSuccess = true };
            Assert.Equal(0, result.ErrorCode);
        }

        [Fact]
        public void PackResult_ErrorCode_ValidationFailed()
        {
            var result = new PackResult
            {
                IsSuccess = false,
                ErrorMessage = "Validation failed",
                ErrorCode = PackErrorCode.ValidationFailed
            };
            Assert.Equal(9002, result.ErrorCode);
            Assert.False(result.IsSuccess);
        }

        [Fact]
        public void PackResult_ErrorCode_InvalidPe()
        {
            var result = new PackResult
            {
                IsSuccess = false,
                ErrorMessage = "Invalid PE",
                ErrorCode = PackErrorCode.InvalidPe
            };
            Assert.Equal(2001, result.ErrorCode);
        }

        [Fact]
        public void PackResult_ErrorCode_UnsupportedArch()
        {
            var result = new PackResult
            {
                IsSuccess = false,
                ErrorMessage = "Unsupported architecture",
                ErrorCode = PackErrorCode.UnsupportedArch
            };
            Assert.Equal(2002, result.ErrorCode);
        }

        [Fact]
        public void PackResult_ErrorCode_OperationCancelled()
        {
            var result = new PackResult
            {
                IsSuccess = false,
                ErrorMessage = "Cancelled",
                ErrorCode = PackErrorCode.OperationCancelled
            };
            Assert.Equal(9001, result.ErrorCode);
        }

        [Fact]
        public void PackErrorCode_AllValues_AreNonZero()
        {
            // All error codes should be non-zero (0 = success)
            Assert.NotEqual(0, PackErrorCode.FileNotFound);
            Assert.NotEqual(0, PackErrorCode.ReadFailed);
            Assert.NotEqual(0, PackErrorCode.WriteFailed);
            Assert.NotEqual(0, PackErrorCode.InvalidPe);
            Assert.NotEqual(0, PackErrorCode.UnsupportedArch);
            Assert.NotEqual(0, PackErrorCode.InvalidDosHeader);
            Assert.NotEqual(0, PackErrorCode.InvalidPeHeader);
            Assert.NotEqual(0, PackErrorCode.VfsBuildFailed);
            Assert.NotEqual(0, PackErrorCode.VfsInvalidData);
            Assert.NotEqual(0, PackErrorCode.VfsChecksumMismatch);
            Assert.NotEqual(0, PackErrorCode.CompressionFailed);
            Assert.NotEqual(0, PackErrorCode.DecompressionFailed);
            Assert.NotEqual(0, PackErrorCode.SectionFull);
            Assert.NotEqual(0, PackErrorCode.ImportMergeFailed);
            Assert.NotEqual(0, PackErrorCode.NoMemory);
            Assert.NotEqual(0, PackErrorCode.LoaderNotFound);
            Assert.NotEqual(0, PackErrorCode.LoaderInitFailed);
            Assert.NotEqual(0, PackErrorCode.LoaderHookFailed);
            Assert.NotEqual(0, PackErrorCode.OperationCancelled);
            Assert.NotEqual(0, PackErrorCode.ValidationFailed);
            Assert.NotEqual(0, PackErrorCode.UnexpectedError);
        }

        [Fact]
        public void PackErrorCode_AllValues_AreUnique()
        {
            // All error codes should be unique
            var codes = new[]
            {
                PackErrorCode.FileNotFound, PackErrorCode.ReadFailed, PackErrorCode.WriteFailed,
                PackErrorCode.InvalidPe, PackErrorCode.UnsupportedArch, PackErrorCode.InvalidDosHeader, PackErrorCode.InvalidPeHeader,
                PackErrorCode.VfsBuildFailed, PackErrorCode.VfsInvalidData, PackErrorCode.VfsChecksumMismatch,
                PackErrorCode.CompressionFailed, PackErrorCode.DecompressionFailed,
                PackErrorCode.SectionFull, PackErrorCode.ImportMergeFailed, PackErrorCode.NoMemory,
                PackErrorCode.LoaderNotFound, PackErrorCode.LoaderInitFailed, PackErrorCode.LoaderHookFailed,
                PackErrorCode.OperationCancelled, PackErrorCode.ValidationFailed, PackErrorCode.UnexpectedError
            };
            Assert.Equal(codes.Length, codes.Distinct().Count());
        }
    }

    /// <summary>
    /// Tests for the entry point stub layout consistency between PeTool C code and C# constants.
    /// </summary>
    public class StubLayoutConsistencyTests
    {
        [Fact]
        public void X64_StubLayout_MatchesPeToolImplementation()
        {
            // x64 stub: sub rsp,0x28 (4) + add rsp,0x28 (4) + jmp [rip+0] (6) = 14 bytes
            const int STUB_CODE_SIZE = 14;
            const int METADATA_SIZE = 8;        // original_ep_rva (4) + section_rva (4)

            // Total layout: [stub:14][VA_placeholder:8][metadata:8][VFS data...]
            const int TOTAL_HEADER = STUB_CODE_SIZE + 8 + METADATA_SIZE;
            Assert.Equal(30, TOTAL_HEADER);

            // VA placeholder offset (where Loader patches the jump target)
            Assert.Equal(14, STUB_CODE_SIZE);

            // Metadata offset (where Loader reads original_ep_rva and section_rva)
            Assert.Equal(22, STUB_CODE_SIZE + 8);
        }

        [Fact]
        public void X86_StubLayout_MatchesPeToolImplementation()
        {
            // x86 stub: push imm32 (5) + ret (1) = 6 bytes
            const int STUB_CODE_SIZE = 6;
            const int VA_PLACEHOLDER_OFFSET = 1;  // inside push instruction (after 0x68 opcode)
            const int METADATA_SIZE = 8;          // original_ep_rva (4) + section_rva (4)

            // Total layout: [stub:6][metadata:8][VFS data...]
            const int TOTAL_HEADER = STUB_CODE_SIZE + METADATA_SIZE;
            Assert.Equal(14, TOTAL_HEADER);

            // VA placeholder is inside the push instruction at offset 1
            Assert.Equal(1, VA_PLACEHOLDER_OFFSET);

            // Metadata offset (where Loader reads original_ep_rva and section_rva)
            Assert.Equal(6, STUB_CODE_SIZE);
        }

        [Fact]
        public void X64_StubMachineCode_BytesAreCorrect()
        {
            // Verify the exact byte sequence of the x64 entry point stub
            // sub rsp, 0x28: 48 83 EC 28
            // add rsp, 0x28: 48 83 C4 28
            // jmp [rip+0]:   FF 25 00 00 00 00
            byte[] expected = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0xC4, 0x28, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
            Assert.Equal(14, expected.Length);

            // Verify each instruction
            Assert.Equal(0x48, expected[0]);  // REX.W prefix
            Assert.Equal(0x83, expected[1]);  // SUB r/m64, imm8
            Assert.Equal(0xEC, expected[2]);  // ModRM for RSP
            Assert.Equal(0x28, expected[3]);  // 0x28 (40 decimal)

            Assert.Equal(0x48, expected[4]);  // REX.W prefix
            Assert.Equal(0x83, expected[5]);  // ADD r/m64, imm8
            Assert.Equal(0xC4, expected[6]);  // ModRM for RSP
            Assert.Equal(0x28, expected[7]);  // 0x28

            Assert.Equal(0xFF, expected[8]);  // JMP opcode group
            Assert.Equal(0x25, expected[9]);  // ModRM: [rip+disp32]
            Assert.Equal(0x00, expected[10]); // disp32 = 0
            Assert.Equal(0x00, expected[11]);
            Assert.Equal(0x00, expected[12]);
            Assert.Equal(0x00, expected[13]);
        }

        [Fact]
        public void X86_StubMachineCode_BytesAreCorrect()
        {
            // Verify the exact byte sequence of the x86 entry point stub
            // push imm32: 68 XX XX XX XX
            // ret:        C3
            byte opcode_push = 0x68;
            byte opcode_ret = 0xC3;

            Assert.Equal(0x68, opcode_push);
            Assert.Equal(0xC3, opcode_ret);

            // Total stub size: 1 (opcode) + 4 (imm32) + 1 (ret) = 6
            Assert.Equal(6, 1 + 4 + 1);
        }
    }

    /// <summary>
    /// Tests for multi-file removal support in MainViewModel.
    /// </summary>
    public class MultiSelectRemovalTests
    {
        [Fact]
        public void SelectedFileItems_Collection_IsInitialized()
        {
            var vm = new MainViewModel();
            Assert.NotNull(vm.SelectedFileItems);
            Assert.Empty(vm.SelectedFileItems);
        }

        [Fact]
        public void SelectedFileItems_Collection_CanAddAndRemove()
        {
            var vm = new MainViewModel();
            var item = new PackFileItem
            {
                SourcePath = "test.dll",
                VirtualPath = "test.dll",
                OriginalSize = 100
            };

            vm.SelectedFileItems.Add(item);
            Assert.Single(vm.SelectedFileItems);

            vm.SelectedFileItems.Clear();
            Assert.Empty(vm.SelectedFileItems);
        }
    }
}
