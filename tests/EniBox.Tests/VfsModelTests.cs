using System;
using System.IO;
using System.Text;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using Xunit;

namespace EniBox.Tests
{
    public class Crc32Tests
    {
        [Fact]
        public void Compute_EmptyData_ReturnsInitialValue()
        {
            var result = Crc32.Compute(Array.Empty<byte>());
            // Our CRC32 implementation returns 0 for empty data (final XOR applied)
            Assert.Equal(0u, result);
        }

        [Fact]
        public void Compute_KnownData_ReturnsExpectedValue()
        {
            var data = Encoding.ASCII.GetBytes("123456789");
            var result = Crc32.Compute(data);
            Assert.Equal(0xCBF43926u, result); // Standard CRC32 check value
        }

        [Fact]
        public void Compute_SameDataTwice_ReturnsSameResult()
        {
            var data = Encoding.ASCII.GetBytes("Hello, World!");
            var r1 = Crc32.Compute(data);
            var r2 = Crc32.Compute(data);
            Assert.Equal(r1, r2);
        }

        [Fact]
        public void Compute_DifferentData_ReturnsDifferentResult()
        {
            var d1 = Encoding.ASCII.GetBytes("foo");
            var d2 = Encoding.ASCII.GetBytes("bar");
            Assert.NotEqual(Crc32.Compute(d1), Crc32.Compute(d2));
        }
    }

    public class VfsHeaderTests
    {
        [Fact]
        public void WriteAndRead_Roundtrip_PreservesValues()
        {
            var header = new VfsHeader
            {
                Magic = VfsHeader.MAGIC,
                Version = VfsHeader.CURRENT_VERSION,
                FileCount = 10,
                DirCount = 5,
                MetadataOffset = 100,
                MetadataSize = 200,
                DataOffset = 300,
                DataSize = 400,
                LoaderOffset = 700,
                LoaderSize = 50,
                Checksum = 0xABCDEF01
            };

            using var ms = new MemoryStream();
            using (var writer = new BinaryWriter(ms, Encoding.UTF8, true))
            {
                header.WriteTo(writer);
            }

            ms.Position = 0;
            using var reader = new BinaryReader(ms);
            var read = VfsHeader.ReadFrom(reader);

            Assert.Equal(header.Magic, read.Magic);
            Assert.Equal(header.Version, read.Version);
            Assert.Equal(header.FileCount, read.FileCount);
            Assert.Equal(header.DirCount, read.DirCount);
            Assert.Equal(header.MetadataOffset, read.MetadataOffset);
            Assert.Equal(header.MetadataSize, read.MetadataSize);
            Assert.Equal(header.DataOffset, read.DataOffset);
            Assert.Equal(header.DataSize, read.DataSize);
            Assert.Equal(header.LoaderOffset, read.LoaderOffset);
            Assert.Equal(header.LoaderSize, read.LoaderSize);
            Assert.Equal(header.Checksum, read.Checksum);
        }

        [Fact]
        public void IsValid_CorrectMagic_ReturnsTrue()
        {
            var header = new VfsHeader { Magic = VfsHeader.MAGIC, Version = 1 };
            Assert.True(header.IsValid());
        }

        [Fact]
        public void IsValid_WrongMagic_ReturnsFalse()
        {
            var header = new VfsHeader { Magic = 0xDEADBEEF, Version = 1 };
            Assert.False(header.IsValid());
        }
    }

    public class VfsFileEntryTests
    {
        [Fact]
        public void WriteAndRead_Roundtrip_PreservesValues()
        {
            var entry = new VfsFileEntry
            {
                NameOffset = 42,
                DirIndex = 3,
                DataOffset = 1000,
                DataSize = 500,
                OriginalSize = 800,
                Attributes = 32,
                LastWriteTime = 1234567890,
                IsCompressed = 1,
                IsVirtualized = 1
            };

            using var ms = new MemoryStream();
            using (var writer = new BinaryWriter(ms, Encoding.UTF8, true))
            {
                entry.WriteTo(writer);
            }

            ms.Position = 0;
            using var reader = new BinaryReader(ms);
            var read = VfsFileEntry.ReadFrom(reader);

            Assert.Equal(entry.NameOffset, read.NameOffset);
            Assert.Equal(entry.DirIndex, read.DirIndex);
            Assert.Equal(entry.DataOffset, read.DataOffset);
            Assert.Equal(entry.DataSize, read.DataSize);
            Assert.Equal(entry.OriginalSize, read.OriginalSize);
            Assert.Equal(entry.Attributes, read.Attributes);
            Assert.Equal(entry.LastWriteTime, read.LastWriteTime);
            Assert.Equal(entry.IsCompressed, read.IsCompressed);
            Assert.Equal(entry.IsVirtualized, read.IsVirtualized);
        }
    }

    public class VfsDirEntryTests
    {
        [Fact]
        public void WriteAndRead_Roundtrip_PreservesValues()
        {
            var entry = new VfsDirEntry
            {
                NameOffset = 10,
                ParentIndex = VfsDirEntry.INVALID_INDEX,
                FirstChild = 1,
                NextSibling = VfsDirEntry.INVALID_INDEX,
                FirstFile = 0
            };

            using var ms = new MemoryStream();
            using (var writer = new BinaryWriter(ms, Encoding.UTF8, true))
            {
                entry.WriteTo(writer);
            }

            ms.Position = 0;
            using var reader = new BinaryReader(ms);
            var read = VfsDirEntry.ReadFrom(reader);

            Assert.Equal(entry.NameOffset, read.NameOffset);
            Assert.Equal(entry.ParentIndex, read.ParentIndex);
            Assert.Equal(entry.FirstChild, read.FirstChild);
            Assert.Equal(entry.NextSibling, read.NextSibling);
            Assert.Equal(entry.FirstFile, read.FirstFile);
        }
    }

    public class PackFileItemTests
    {
        [Fact]
        public void FromFile_CreatesValidItem()
        {
            // Create a temp file
            var tempFile = Path.GetTempFileName();
            try
            {
                File.WriteAllText(tempFile, "test content");
                var baseDir = Path.GetDirectoryName(tempFile)!;
                var item = PackFileItem.FromFile(tempFile, baseDir);

                Assert.Equal(tempFile, item.SourcePath);
                Assert.True(item.IsCompressed);
                Assert.True(item.IsVirtualized);
                Assert.True(item.OriginalSize > 0);
            }
            finally
            {
                File.Delete(tempFile);
            }
        }
    }

    public class PackConfigurationTests
    {
        [Fact]
        public void DefaultValues_AreCorrect()
        {
            var config = new PackConfiguration();
            Assert.Equal(string.Empty, config.SourceExePath);
            Assert.Equal(string.Empty, config.OutputPath);
            Assert.Empty(config.Files);
            Assert.False(config.EnableRegistryVirtualization);
            Assert.True(config.EnableSubProcessInjection);
        }
    }

    public class PackProgressTests
    {
        [Fact]
        public void DefaultStage_IsCollectingFiles()
        {
            var progress = new PackProgress { Stage = PackStage.CollectingFiles };
            Assert.Equal(PackStage.CollectingFiles, progress.Stage);
        }
    }

    public class PackResultTests
    {
        [Fact]
        public void SuccessResult_HasCorrectProperties()
        {
            var result = new PackResult { IsSuccess = true, OutputPath = "out.exe", OutputFileSize = 1024 };
            Assert.True(result.IsSuccess);
            Assert.Equal("out.exe", result.OutputPath);
            Assert.Equal(1024, result.OutputFileSize);
        }

        [Fact]
        public void FailureResult_HasErrorMessage()
        {
            var result = new PackResult { IsSuccess = false, ErrorMessage = "Test error" };
            Assert.False(result.IsSuccess);
            Assert.Equal("Test error", result.ErrorMessage);
        }
    }

    public class PeArchitectureTests
    {
        [Fact]
        public void X86Value_IsCorrect()
        {
            Assert.Equal(0x014C, (int)PeArchitecture.X86);
        }

        [Fact]
        public void X64Value_IsCorrect()
        {
            Assert.Equal(0x8664, (int)PeArchitecture.X64);
        }
    }

    public class PeInfoTests
    {
        [Fact]
        public void Is64Bit_ReturnsCorrectValue()
        {
            var info32 = new PeInfo { Architecture = PeArchitecture.X86 };
            var info64 = new PeInfo { Architecture = PeArchitecture.X64 };
            Assert.False(info32.Is64Bit);
            Assert.True(info64.Is64Bit);
        }
    }

    public class PackExceptionTests
    {
        [Fact]
        public void Constructor_SetsErrorCode()
        {
            var ex = new PackException(2001, "PE error");
            Assert.Equal(2001, ex.ErrorCode);
            Assert.Equal("PE error", ex.Message);
        }

        [Fact]
        public void ConstructorWithInnerException_PreservesInner()
        {
            var inner = new InvalidOperationException("inner");
            var ex = new PackException(1001, "outer", inner);
            Assert.Equal(1001, ex.ErrorCode);
            Assert.Same(inner, ex.InnerException);
        }
    }
}
