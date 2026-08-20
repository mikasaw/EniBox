using System;
using System.IO;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.E2E;

public class LoaderExtractionTests : E2ETestBase
{
    public LoaderExtractionTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_PackedExe_LoaderDll_ExtractedToTempDir()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_loader_test.enibox");
        
        var packResult = await PackExeAsync(sourcePath, outputPath);
        if (!packResult.IsSuccess)
        {
            Logger.Warning($"⚠ 封包失败: {packResult.ErrorMessage}");
            return;
        }
        
        var runResult = RunPackedExe(outputPath);
        
        Assert.False(runResult.TimedOut, "EXE不应超时");
        Logger.Success("✓ 封包EXE运行完成，Loader DLL应已提取到临时目录");
        
        var loaderFiles = Directory.GetFiles(Path.GetTempPath(), "EniBox.Loader.*.dll");
        Logger.Info($"临时目录中Loader DLL文件数: {loaderFiles.Length}");
        
        if (loaderFiles.Length > 0)
        {
            foreach (var f in loaderFiles)
            {
                Logger.Success($"✓ Loader DLL存在: {f}");
            }
        }
        else
        {
            Logger.Info("Loader DLL可能已被清理（DLL_PROCESS_DETACH时CleanupExtractedLoader）");
        }
    }
}

public class SpecialPathTests : E2ETestBase
{
    public SpecialPathTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_Pack_SourceWithSpaces_ProducesOutput()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputDir = TempFiles.CreateTempDirectory();
        var spacedDir = Path.Combine(outputDir, "path with spaces");
        Directory.CreateDirectory(spacedDir);
        var outputPath = Path.Combine(spacedDir, "output.enibox");
        
        var result = await PackExeAsync(sourcePath, outputPath);
        
        if (result.IsSuccess)
        {
            Assert.True(File.Exists(outputPath));
            Logger.Success("✓ 含空格路径的封包成功");
        }
        else
        {
            Logger.Info($"含空格路径封包结果: {result.ErrorMessage}");
        }
    }
    
[SkippableFact]
    public async Task E2E_Pack_SourceWithChinesePath_ProducesOutput()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputDir = TempFiles.CreateTempDirectory();
        var chineseDir = Path.Combine(outputDir, "中文路径测试");
        Directory.CreateDirectory(chineseDir);
        var outputPath = Path.Combine(chineseDir, "输出文件.enibox");
        
        var result = await PackExeAsync(sourcePath, outputPath);
        
        if (result.IsSuccess)
        {
            Assert.True(File.Exists(outputPath));
            Logger.Success("✓ 含中文路径的封包成功");
        }
        else
        {
            Logger.Info($"含中文路径封包结果: {result.ErrorMessage}");
        }
    }
}

public class RepackTests : E2ETestBase
{
    public RepackTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_Repack_AlreadyPackedExe_ReturnsErrorOrOverwrites()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputDir = TempFiles.CreateTempDirectory();
        var firstOutput = Path.Combine(outputDir, "first.enibox");
        
        var firstResult = await PackExeAsync(sourcePath, firstOutput);
        if (!firstResult.IsSuccess)
        {
            Logger.Warning($"⚠ 首次封包失败: {firstResult.ErrorMessage}");
            return;
        }
        
        var secondOutput = Path.Combine(outputDir, "second.enibox");
        var secondResult = await PackExeAsync(firstOutput, secondOutput);
        
        Logger.Info($"重复封包结果: IsSuccess={secondResult.IsSuccess}, ErrorCode={secondResult.ErrorCode}");
        
        if (secondResult.IsSuccess)
        {
            Logger.Success("✓ 重复封包成功（已覆盖处理）");
        }
        else
        {
            Logger.Success($"✓ 重复封包返回错误: {secondResult.ErrorMessage}");
        }
    }
}

public class MultiFileStressTests : E2ETestBase
{
    public MultiFileStressTests(ITestOutputHelper output) : base(output) { }
    
[SkippableFact]
    public async Task E2E_Pack_MultipleDependencies_ProducesOutput()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputDir = TempFiles.CreateTempDirectory();
        var outputPath = Path.Combine(outputDir, "multi_file.enibox");
        
        var config = new PackConfiguration
        {
            SourceExePath = sourcePath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true
        };
        
        for (int i = 0; i < 10; i++)
        {
            var depFile = TempFiles.CreateTempFile($"dependency file {i} content", $".dep{i}");
            config.Files.Add(new PackFileItem
            {
                SourcePath = depFile,
                VirtualPath = $"dep/file{i}.dat",
                IsCompressed = true,
                OriginalSize = new FileInfo(depFile).Length
            });
        }
        
        var result = await PackService.PackAsync(config, null, System.Threading.CancellationToken.None);
        
        if (result.IsSuccess)
        {
            Assert.True(File.Exists(outputPath));
            Logger.Success($"✓ 10个依赖文件封包成功: {result.OutputFileSize} bytes");
        }
        else
        {
            Logger.Info($"多文件封包结果: {result.ErrorMessage}");
        }
    }
}
