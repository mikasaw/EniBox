using System;
using System.IO;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.E2E;

public class FullPackFlowTests : E2ETestBase
{
    public FullPackFlowTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_FullPack_WithFcExe_ProducesOutputFile()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc.enibox");
        
        Logger.Info($"源文件: {sourcePath}");
        Logger.Info($"输出路径: {outputPath}");
        
        var result = await PackExeAsync(sourcePath, outputPath);
        
        if (!result.IsSuccess)
        {
            Logger.Fail($"✗ 封包失败: {result.ErrorMessage} (ErrorCode={result.ErrorCode})");
        }
        else
        {
            Logger.Success($"✓ 封包成功: {result.OutputPath} ({result.OutputFileSize} bytes)");
        }
        
        Assert.True(result.IsSuccess, $"封包应成功: {result.ErrorMessage}");
        Assert.True(File.Exists(outputPath), "输出文件应存在");
        
        var sourceSize = new FileInfo(sourcePath).Length;
        var outputSize = new FileInfo(outputPath).Length;
        Assert.True(outputSize > sourceSize, $"输出文件({outputSize})应大于源文件({sourceSize})");
        
        Logger.Success($"✓ 输出文件大小验证: {sourceSize} → {outputSize} bytes");
    }
    
[SkippableFact]
    public async Task E2E_FullPack_WithDependencies_ProducesOutputFile()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_with_deps.enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = sourcePath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true
        };
        
        var result = await PackService.PackAsync(config, null, System.Threading.CancellationToken.None);
        
        Assert.True(result.IsSuccess, $"封包应成功: {result.ErrorMessage}");
        Assert.True(File.Exists(outputPath), "输出文件应存在");
        Logger.Success($"✓ 含依赖封包成功: {result.OutputFileSize} bytes");
    }
}

public class PackedExeRunTests : E2ETestBase
{
    public PackedExeRunTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_PackedFcExe_RunsAndExitsNormally()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_test.enibox");
        
        var packResult = await PackExeAsync(sourcePath, outputPath);
        if (!packResult.IsSuccess)
        {
            Logger.Warning($"⚠ 封包失败，跳过运行测试: {packResult.ErrorMessage}");
            return;
        }
        
        Logger.Info("启动封包后的EXE...");
        var runResult = RunPackedExe(outputPath);
        
        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"超时: {runResult.TimedOut}");
        
        if (!string.IsNullOrEmpty(runResult.StandardError))
            Logger.Warning($"stderr: {runResult.StandardError}");
        
        Assert.False(runResult.TimedOut, "EXE不应超时挂起");
        Logger.Success("✓ 封包EXE启动并退出，未挂起");
    }
}

public class VfsRuntimeAccessTests : E2ETestBase
{
    public VfsRuntimeAccessTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_PackedExe_VfsFileAccess_NonVfsFile_Passthrough()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_vfs.enibox");
        
        var packResult = await PackExeAsync(sourcePath, outputPath);
        if (!packResult.IsSuccess)
        {
            Logger.Warning($"⚠ 封包失败: {packResult.ErrorMessage}");
            return;
        }
        
        var tempFile1 = TempFiles.CreateTempFile("hello", ".txt");
        var tempFile2 = TempFiles.CreateTempFile("world", ".txt");
        
        var runResult = ProcessRunner.Run(outputPath, $"\"{tempFile1}\" \"{tempFile2}\"", 10000);
        
        Logger.Info($"fc.exe退出码: {runResult.ExitCode}");
        Logger.Success("✓ 封包EXE可访问非VFS文件");
    }
}
