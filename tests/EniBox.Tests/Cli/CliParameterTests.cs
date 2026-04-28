using System;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Cli;

public class CliParameterTests : VerificationTestBase
{
    public CliParameterTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void CliRunner_CanBeInstantiated()
    {
        var cli = new CliRunner();
        Assert.NotNull(cli);
        Logger.Success("✓ CliRunner可实例化");
    }
    
    [Fact]
    public void CliRunner_MissingSource_ReturnsNonZeroExitCode()
    {
        var cli = new CliRunner();
        var exitCode = cli.Run(new[] { "--output", "out.exe" });
        
        Assert.NotEqual(0, exitCode);
        Logger.Success($"✓ 缺少--source返回非零退出码: {exitCode}");
    }
    
    [Fact]
    public void CliRunner_MissingOutput_ReturnsNonZeroExitCode()
    {
        var cli = new CliRunner();
        var exitCode = cli.Run(new[] { "--source", "app.exe" });
        
        Assert.NotEqual(0, exitCode);
        Logger.Success($"✓ 缺少--output返回非零退出码: {exitCode}");
    }
    
    [Fact]
    public void CliRunner_Help_ReturnsZeroExitCode()
    {
        var cli = new CliRunner();
        var exitCode = cli.Run(new[] { "--help" });
        
        Assert.Equal(0, exitCode);
        Logger.Success("✓ --help返回退出码0");
    }
    
    [Fact]
    public void CliRunner_NoArgs_ReturnsNonZeroExitCode()
    {
        var cli = new CliRunner();
        var exitCode = cli.Run(Array.Empty<string>());
        
        Assert.NotEqual(0, exitCode);
        Logger.Success($"✓ 无参数返回非零退出码: {exitCode}");
    }
}

public class CliParameterOptionalTests : VerificationTestBase, IDisposable
{
    private readonly TempFileHelper _tempFiles;
    
    public CliParameterOptionalTests(ITestOutputHelper output) : base(output)
    {
        _tempFiles = new TempFileHelper();
    }
    
    [Fact]
    public void CliRunner_NonExistentSource_ReturnsNonZeroExitCode()
    {
        var cli = new CliRunner();
        var exitCode = cli.Run(new[] { "--source", @"C:\nonexistent_app.exe", "--output", "out.enibox" });
        
        Assert.NotEqual(0, exitCode);
        Logger.Success($"✓ 不存在的源文件返回非零退出码: {exitCode}");
    }
    
    [Fact]
    public void PackProgress_DefaultValues_AreCorrect()
    {
        var progress = new PackProgress();
        
        Assert.Equal(0, progress.TotalFiles);
        Assert.Equal(0, progress.ProcessedFiles);
        Assert.Equal(string.Empty, progress.CurrentFile);
        Assert.Equal(PackStage.CollectingFiles, progress.Stage);
        Assert.Equal(0, progress.ProgressPercent);
    }
    
    [Fact]
    public void PackStage_Enum_HasAllStages()
    {
        var stages = Enum.GetValues<PackStage>();
        
        Assert.Equal(7, stages.Length);
        Assert.Contains(PackStage.CollectingFiles, stages);
        Assert.Contains(PackStage.BuildingVFS, stages);
        Assert.Contains(PackStage.Compressing, stages);
        Assert.Contains(PackStage.ModifyingPE, stages);
        Assert.Contains(PackStage.WritingOutput, stages);
        Assert.Contains(PackStage.Completed, stages);
        Assert.Contains(PackStage.Failed, stages);
    }
    
    [Fact]
    public void VfsBuildResult_DefaultValues_AreCorrect()
    {
        var result = new VfsBuildResult();
        
        Assert.Empty(result.Metadata);
        Assert.Empty(result.DataRegion);
        Assert.Equal(0, result.FileCount);
        Assert.Equal(0, result.DirCount);
        Assert.Equal(0, result.TotalOriginalSize);
        Assert.Equal(0, result.TotalCompressedSize);
    }
    
    public new void Dispose()
    {
        _tempFiles.Dispose();
        base.Dispose();
    }
}
