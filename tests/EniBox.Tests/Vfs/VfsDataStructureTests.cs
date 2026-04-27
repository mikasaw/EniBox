using System;
using System.IO;
using EniBox.GUI.Models;
using Xunit;

namespace EniBox.Tests.Vfs;

public class VfsHeaderSerializationTests
{
    [Fact]
    public void WriteTo_ReadFrom_RoundTrip()
    {
        var original = new VfsHeader
        {
            Magic = VfsHeader.MAGIC,
            Version = VfsHeader.CURRENT_VERSION,
            FileCount = 10,
            DirCount = 3,
            MetadataOffset = 1024,
            MetadataSize = 512,
            DataOffset = 2048,
            DataSize = 4096,
            LoaderOffset = 8192,
            LoaderSize = 256,
            Checksum = 0xABCDEF01
        };
        
        using var ms = new MemoryStream();
        using (var writer = new BinaryWriter(ms, System.Text.Encoding.Default, leaveOpen: true))
        {
            original.WriteTo(writer);
        }
        
        ms.Position = 0;
        VfsHeader read;
        using (var reader = new BinaryReader(ms, System.Text.Encoding.Default, leaveOpen: true))
        {
            read = VfsHeader.ReadFrom(reader);
        }
        
        Assert.Equal(original.Magic, read.Magic);
        Assert.Equal(original.Version, read.Version);
        Assert.Equal(original.FileCount, read.FileCount);
        Assert.Equal(original.DirCount, read.DirCount);
        Assert.Equal(original.MetadataOffset, read.MetadataOffset);
        Assert.Equal(original.MetadataSize, read.MetadataSize);
        Assert.Equal(original.DataOffset, read.DataOffset);
        Assert.Equal(original.DataSize, read.DataSize);
        Assert.Equal(original.LoaderOffset, read.LoaderOffset);
        Assert.Equal(original.LoaderSize, read.LoaderSize);
        Assert.Equal(original.Checksum, read.Checksum);
    }
    
    [Fact]
    public void IsValid_ReturnsTrueForValidHeader()
    {
        var header = new VfsHeader
        {
            Magic = VfsHeader.MAGIC,
            Version = VfsHeader.CURRENT_VERSION
        };
        
        Assert.True(header.IsValid());
    }
    
    [Fact]
    public void IsValid_ReturnsFalseForInvalidMagic()
    {
        var header = new VfsHeader
        {
            Magic = 0xDEADBEEF,
            Version = 1
        };
        
        Assert.False(header.IsValid());
    }
    
    [Fact]
    public void IsValid_ReturnsFalseForZeroVersion()
    {
        var header = new VfsHeader
        {
            Magic = VfsHeader.MAGIC,
            Version = 0
        };
        
        Assert.False(header.IsValid());
    }
    
    [Fact]
    public void Magic_IsCorrectValue()
    {
        Assert.Equal(0x42494E45u, VfsHeader.MAGIC);
    }
    
    [Fact]
    public void CurrentVersion_IsOne()
    {
        Assert.Equal(1u, VfsHeader.CURRENT_VERSION);
    }
}

public class Crc32Tests
{
    [Fact]
    public void Compute_EmptyData_ReturnsZero()
    {
        var data = Array.Empty<byte>();
        var crc = Crc32.Compute(data);
        Assert.Equal(0u, crc);
    }
    
    [Fact]
    public void Compute_KnownValue_ReturnsExpectedCrc()
    {
        var data = System.Text.Encoding.ASCII.GetBytes("123456789");
        var crc = Crc32.Compute(data);
        Assert.Equal(0xCBF43926u, crc);
    }
    
    [Fact]
    public void Compute_SameData_ReturnsSameCrc()
    {
        var data1 = new byte[] { 0x01, 0x02, 0x03, 0x04 };
        var data2 = new byte[] { 0x01, 0x02, 0x03, 0x04 };
        
        Assert.Equal(Crc32.Compute(data1), Crc32.Compute(data2));
    }
    
    [Fact]
    public void Compute_DifferentData_ReturnsDifferentCrc()
    {
        var data1 = new byte[] { 0x01, 0x02, 0x03 };
        var data2 = new byte[] { 0x03, 0x02, 0x01 };
        
        Assert.NotEqual(Crc32.Compute(data1), Crc32.Compute(data2));
    }
    
    [Fact]
    public void Compute_WithOffsetAndLength_ReturnsCorrectCrc()
    {
        var fullData = System.Text.Encoding.ASCII.GetBytes("123456789");
        var partialData = System.Text.Encoding.ASCII.GetBytes("3456");
        
        var fullCrc = Crc32.Compute(fullData, 2, 4);
        var partialCrc = Crc32.Compute(partialData);
        
        Assert.Equal(partialCrc, fullCrc);
    }
}
