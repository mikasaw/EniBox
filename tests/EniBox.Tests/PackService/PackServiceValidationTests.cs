using System;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using EniBox.Tests.TestInfrastructure;
using Moq;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.PackServiceTests;

public class PackServiceValidationTests : VerificationTestBase, IDisposable
{
    private readonly Mock<ICompressor> _mockCompressor;
    private readonly Mock<IVfsBuilder> _mockVfsBuilder;
    private readonly EniBox.GUI.Services.PackService _packService;
    private readonly TempFileHelper _tempFiles;
    
    public PackServiceValidationTests(ITestOutputHelper output) : base(output)
    {
        _mockCompressor = new Mock<ICompressor>();
        _mockCompressor.Setup(c => c.AlgorithmId).Returns("LZMA");
        
        _mockVfsBuilder = new Mock<IVfsBuilder>();
        _mockVfsBuilder.Setup(b => b.Build()).Returns(new VfsBuildResult());
        
        _packService = new EniBox.GUI.Services.PackService(_mockCompressor.Object, _mockVfsBuilder.Object);
        _tempFiles = new TempFileHelper();
    }
    
    [Fact]
    public async Task PackAsync_EmptySourcePath_ReturnsValidationFailed()
    {
        var config = new PackConfiguration
        {
            SourceExePath = "",
            OutputPath = _tempFiles.CreateTempFile(".exe")
        };
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Assert.Equal(PackErrorCode.ValidationFailed, result.ErrorCode);
        Logger.Success("✓ 空SourceExePath返回ValidationFailed");
    }
    
    [Fact]
    public async Task PackAsync_NonExistentSource_ReturnsValidationFailed()
    {
        var config = new PackConfiguration
        {
            SourceExePath = @"C:\nonexistent_file_that_does_not_exist.exe",
            OutputPath = _tempFiles.CreateTempFile(".exe")
        };
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Assert.Equal(PackErrorCode.ValidationFailed, result.ErrorCode);
        Logger.Success("✓ 不存在的源文件返回ValidationFailed");
    }
    
    [Fact]
    public async Task PackAsync_EmptyOutputPath_ReturnsValidationFailed()
    {
        var sourceFile = _tempFiles.CreateTempFile(".exe");
        var config = new PackConfiguration
        {
            SourceExePath = sourceFile,
            OutputPath = ""
        };
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Assert.Equal(PackErrorCode.ValidationFailed, result.ErrorCode);
        Logger.Success("✓ 空OutputPath返回ValidationFailed");
    }
    
    [Fact]
    public async Task PackAsync_DependencyFileNotFound_ReturnsValidationFailed()
    {
        var sourceFile = _tempFiles.CreateTempFile(".exe");
        var config = new PackConfiguration
        {
            SourceExePath = sourceFile,
            OutputPath = _tempFiles.CreateTempFile(".exe")
        };
        config.Files.Add(new PackFileItem
        {
            SourcePath = @"C:\nonexistent_dependency.dll",
            OriginalSize = 1024
        });
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Assert.Equal(PackErrorCode.ValidationFailed, result.ErrorCode);
        Logger.Success("✓ 依赖文件不存在返回ValidationFailed");
    }
    
    public new void Dispose()
    {
        _tempFiles.Dispose();
        base.Dispose();
    }
}
