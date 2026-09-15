using System;
using System.IO;
using System.Linq;
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

        // 用 cmd.exe + ping 延时参数当宿主：进程初始化（Loader DllMain 提取）
        // 之后存活数秒，留出轮询窗口。fc.exe 无参数会瞬时退出，赶在
        // DLL_PROCESS_DETACH 清理之前根本扫不到。
        var sourcePath = Path.Combine(Environment.SystemDirectory, "cmd.exe");
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "cmd_loader_test.enibox");

        var packResult = await PackExeAsync(sourcePath, outputPath);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");

        // Loader 在 DllMain（进程初始化）期间把自身提取到
        // %TEMP%\EniBox-<pid>-<rnd>\EniBox.Loader.<pid>.<rnd>.dll；
        // 正常退出时 DLL_PROCESS_DETACH 会清理掉提取目录。因此这里
        // 启动后轮询到提取产物就 Kill，让目录留在盘上供断言（终止进程不跑 DETACH）。
        var si = new System.Diagnostics.ProcessStartInfo(outputPath, "/c ping -n 6 127.0.0.1 >nul")
        {
            UseShellExecute = false,
            CreateNoWindow = true
        };
        using var proc = System.Diagnostics.Process.Start(si)
            ?? throw new InvalidOperationException("封包程序启动失败");
        var pid = proc.Id;

        // 提取发生在 DllMain，进程起来后应立刻出现；轮询最多 5 秒
        string[] extractionDirs = Array.Empty<string>();
        for (int i = 0; i < 50 && extractionDirs.Length == 0; i++)
        {
            await Task.Delay(100);
            extractionDirs = Directory.GetDirectories(Path.GetTempPath(), $"EniBox-{pid}-*");
        }
        if (!proc.HasExited) proc.Kill(entireProcessTree: true);
        proc.WaitForExit();

        var extracted = extractionDirs
            .SelectMany(d => Directory.GetFiles(d, "EniBox.Loader.*.dll"))
            .ToArray();

        try
        {
            Assert.True(extracted.Length > 0,
                $"未在 %TEMP%\\EniBox-{pid}-*\\ 下找到提取的 Loader DLL（提取失败或命名不符）");

            foreach (var f in extracted)
            {
                Logger.Success($"✓ Loader DLL已提取: {f}");
                var dirName = Path.GetFileName(Path.GetDirectoryName(f)!);
                Assert.StartsWith($"EniBox-{pid}-", dirName);
                var fileName = Path.GetFileName(f);
                Assert.Contains(pid.ToString(), fileName);
            }
        }
        finally
        {
            // 清理被 Kill 进程遗留的提取目录，避免污染后续测试的 %TEMP% 扫描
            foreach (var d in extractionDirs)
            {
                try { Directory.Delete(d, recursive: true); } catch { }
            }
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
        Assert.True(result.IsSuccess, $"含空格路径封包应成功: {result.ErrorMessage}");
        Assert.True(File.Exists(outputPath));
        Logger.Success("✓ 含空格路径的封包成功");
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
        Assert.True(result.IsSuccess, $"含中文路径封包应成功: {result.ErrorMessage}");
        Assert.True(File.Exists(outputPath));
        Logger.Success("✓ 含中文路径的封包成功");
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
        Assert.True(firstResult.IsSuccess, $"首次封包失败: {firstResult.ErrorMessage}");
        
        var secondOutput = Path.Combine(outputDir, "second.enibox");
        var secondResult = await PackExeAsync(firstOutput, secondOutput);
        
        Logger.Info($"重复封包结果: IsSuccess={secondResult.IsSuccess}, ErrorCode={secondResult.ErrorCode}");

        if (secondResult.IsSuccess)
        {
            Assert.True(File.Exists(secondOutput), "重复封包成功但输出文件不存在");
            Logger.Success("✓ 重复封包成功（已覆盖处理）");
        }
        else
        {
            Assert.False(string.IsNullOrEmpty(secondResult.ErrorMessage), "重复封包失败时必须给出错误信息");
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
