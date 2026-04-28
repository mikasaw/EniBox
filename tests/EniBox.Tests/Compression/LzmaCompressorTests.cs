using System;
using System.Threading.Tasks;
using EniBox.GUI.Services;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Compression;

public class LzmaCompressorTests : IAsyncLifetime
{
    private readonly ITestOutputHelper _output;
    private LzmaCompressor? _compressor;
    
    public LzmaCompressorTests(ITestOutputHelper output)
    {
        _output = output;
    }
    
    public Task InitializeAsync()
    {
        _compressor = new LzmaCompressor();
        return Task.CompletedTask;
    }
    
    public Task DisposeAsync() => Task.CompletedTask;
    
    [Fact]
    public void AlgorithmId_ReturnsLZMA()
    {
        Assert.NotNull(_compressor);
        Assert.Equal("LZMA", _compressor!.AlgorithmId);
    }
    
    [Fact]
    public void CompressDecompress_Roundtrip_PreservesData()
    {
        var original = new byte[] { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
        var compressed = _compressor!.Compress(original);
        var decompressed = _compressor.Decompress(compressed, original.Length);
        
        Assert.Equal(original, decompressed);
    }
    
    [Fact]
    public void Compress_ProducesSmallerOutput_ForCompressibleData()
    {
        var data = new byte[1024];
        for (int i = 0; i < data.Length; i++)
            data[i] = 0x41;
        
        var compressed = _compressor!.Compress(data);
        
        Assert.True(compressed.Length < data.Length, 
            $"压缩后({compressed.Length})应小于原始({data.Length})");
    }
    
    [Fact]
    public void CompressDecompress_SmallData_PreservesData()
    {
        var original = new byte[] { 0x42 };
        var compressed = _compressor!.Compress(original);
        var decompressed = _compressor.Decompress(compressed, original.Length);
        
        Assert.Equal(original, decompressed);
    }
    
    [Fact]
    public void CompressDecompress_BinaryData_PreservesData()
    {
        var rng = new Random(42);
        var original = new byte[256];
        rng.NextBytes(original);
        
        var compressed = _compressor!.Compress(original);
        var decompressed = _compressor.Decompress(compressed, original.Length);
        
        Assert.Equal(original, decompressed);
    }
}
