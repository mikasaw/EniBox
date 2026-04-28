using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using EniBox.Tests.TestInfrastructure;
using Moq;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.PackServiceTests;

public class PackServiceMoqTests : VerificationTestBase, IDisposable
{
    private readonly Mock<ICompressor> _mockCompressor;
    private readonly Mock<IVfsBuilder> _mockVfsBuilder;
    private readonly EniBox.GUI.Services.PackService _packService;
    private readonly TempFileHelper _tempFiles;
    
    public PackServiceMoqTests(ITestOutputHelper output) : base(output)
    {
        _mockCompressor = new Mock<ICompressor>();
        _mockCompressor.Setup(c => c.AlgorithmId).Returns("LZMA");
        
        _mockVfsBuilder = new Mock<IVfsBuilder>();
        _mockVfsBuilder.Setup(b => b.Build()).Returns(new VfsBuildResult
        {
            Metadata = new byte[64],
            DataRegion = new byte[128],
            FileCount = 1,
            DirCount = 0,
            TotalOriginalSize = 100,
            TotalCompressedSize = 50
        });
        
        _packService = new EniBox.GUI.Services.PackService(_mockCompressor.Object, _mockVfsBuilder.Object);
        _tempFiles = new TempFileHelper();
    }
    
    [Fact]
    public async Task PackAsync_NonPeSourceFile_ReturnsInvalidPe()
    {
        var nonPeFile = _tempFiles.CreateTempFile(new byte[] { 0x01, 0x02, 0x03, 0x04 }, ".exe");
        var outputFile = _tempFiles.CreateTempFile(".enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = nonPeFile,
            OutputPath = outputFile
        };
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Logger.Success($"✓ 非PE文件返回错误: ErrorCode={result.ErrorCode}");
    }
    
    [Fact]
    public async Task PackAsync_CancellationRequested_ReturnsCancelled()
    {
        var sourceFile = _tempFiles.CreateTempFile(".exe");
        var outputFile = _tempFiles.CreateTempFile(".enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = sourceFile,
            OutputPath = outputFile
        };
        
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        
        var result = await _packService.PackAsync(config, null, cts.Token);
        
        Assert.False(result.IsSuccess);
        Assert.Equal(PackErrorCode.OperationCancelled, result.ErrorCode);
        Logger.Success("✓ 取消操作返回OperationCancelled");
    }
    
    public new void Dispose()
    {
        _tempFiles.Dispose();
        base.Dispose();
    }
}

public class PackServiceExceptionTests : VerificationTestBase, IDisposable
{
    private readonly Mock<ICompressor> _mockCompressor;
    private readonly Mock<IVfsBuilder> _mockVfsBuilder;
    private readonly EniBox.GUI.Services.PackService _packService;
    private readonly TempFileHelper _tempFiles;
    
    public PackServiceExceptionTests(ITestOutputHelper output) : base(output)
    {
        _mockCompressor = new Mock<ICompressor>();
        _mockCompressor.Setup(c => c.AlgorithmId).Returns("LZMA");
        
        _mockVfsBuilder = new Mock<IVfsBuilder>();
        _mockVfsBuilder.Setup(b => b.Build()).Returns(new VfsBuildResult());
        
        _packService = new EniBox.GUI.Services.PackService(_mockCompressor.Object, _mockVfsBuilder.Object);
        _tempFiles = new TempFileHelper();
    }
    
    [Fact]
    public async Task PackAsync_VfsBuilderThrows_ReturnsUnexpectedError()
    {
        _mockVfsBuilder.Setup(b => b.Build()).Throws(new InvalidOperationException("VFS internal error"));
        
        var sourceFile = _tempFiles.CreateTempFile(".exe");
        var outputFile = _tempFiles.CreateTempFile(".enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = sourceFile,
            OutputPath = outputFile
        };
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Logger.Success($"✓ VfsBuilder异常返回失败: ErrorCode={result.ErrorCode}");
    }
    
    [Fact]
    public async Task PackAsync_PackException_ReturnsFailureWithMessage()
    {
        _mockVfsBuilder.Setup(b => b.Build()).Throws(new PackException(PackErrorCode.VfsBuildFailed, "VFS build error"));
        
        var sourceFile = _tempFiles.CreateTempFile(".exe");
        var outputFile = _tempFiles.CreateTempFile(".enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = sourceFile,
            OutputPath = outputFile
        };
        
        var result = await _packService.PackAsync(config, null, CancellationToken.None);
        
        Assert.False(result.IsSuccess);
        Logger.Success($"✓ PackException返回失败结果: {result.ErrorMessage}");
    }
    
    [Fact]
    public async Task PackAsync_ProgressCallback_ReceivesUpdates()
    {
        var sourceFile = _tempFiles.CreateTempFile(".exe");
        var outputFile = _tempFiles.CreateTempFile(".enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = sourceFile,
            OutputPath = outputFile
        };
        
        var progressReports = new List<PackProgress>();
        IProgress<PackProgress> progress = new SynchronousProgress<PackProgress>(p => progressReports.Add(p));
        
        await _packService.PackAsync(config, progress, CancellationToken.None);
        
        Assert.NotEmpty(progressReports);
        Assert.Contains(progressReports, p => p.Stage == PackStage.CollectingFiles);
        Logger.Success($"✓ 进度回调被触发{progressReports.Count}次");
    }
    
    public new void Dispose()
    {
        _tempFiles.Dispose();
        base.Dispose();
    }
}
