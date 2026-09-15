using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.E2E;

/// <summary>
/// End-to-end tests that pack an EXE with embedded files and run it to verify
/// the VFS runtime correctly serves file data through API hooks.
/// These tests cover the critical gap of verifying VFS file access through
/// a packed executable, rather than just testing helpers in isolation.
/// </summary>
public class PackedVfsRuntimeTests : E2ETestBase
{
    public PackedVfsRuntimeTests(ITestOutputHelper output) : base(output) { }

    /// <summary>
    /// 验证: 封包后的程序通过 VFS 读取嵌入文件
    /// 步骤: 编译 FileChecker 辅助程序 → 将其与数据文件一起封包 → 运行封包后程序 → 确认读到嵌入内容
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedFileChecker_ReadsEmbeddedFileViaVfs()
    {
        RequirePeTool();
        RequireHelper("FileChecker");

        var fcPath = TestExeBuilder.GetHelperPath("FileChecker");
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_vfs_file.enibox");

        // 创建一个测试文件，我们将它嵌入 VFS
        var testContent = "Hello from EniBox VFS runtime! This file was read through API hooks.";
        var testFilePath = TempFiles.CreateTempFile(testContent, ".dat");

        // 封包 FileChecker + 嵌入数据文件 (VirtualPath 使用实际路径以便 VFS 匹配)
        var config = new PackConfiguration
        {
            SourceExePath = fcPath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true,
            EnableRegistryVirtualization = false
        };
        config.Files.Add(new PackFileItem
        {
            SourcePath = testFilePath,
            VirtualPath = testFilePath,  // 使用绝对路径，VFS 运行时通过此路径匹配
            IsCompressed = false,
            OriginalSize = new FileInfo(testFilePath).Length
        });

        var packResult = await PackService.PackAsync(config, null, CancellationToken.None);
        // PeTool 缺失由 RequirePeTool 挡住并 Skip；走到这里封包就必须成功，
        // 失败说明管线坏了，必须 FAIL（静默 return 是假绿）。
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");
        Logger.Success($"✓ 封包成功: {packResult.OutputFileSize} bytes");

        // 运行封包后的 FileChecker，传入嵌入文件的路径
        var runResult = ProcessRunner.Run(outputPath, $"\"{testFilePath}\"", 15000);

        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出: {runResult.StandardOutput}");

        // FileChecker 应成功打开并读取文件内容（通过 VFS Hook 拦截）
        Assert.Equal(0, runResult.ExitCode);
        Assert.Contains("CHECK:FILE_READ:OK", runResult.StandardOutput);
        Assert.Contains("Hello from EniBox VFS runtime!", runResult.StandardOutput);
        Logger.Success("✓ 封包 FileChecker 通过 VFS 成功读取嵌入文件");
    }

    /// <summary>
    /// 验证: 封包后的程序读取仅存在于 VFS 中的文件（磁盘上不存在该文件）
    /// 这严格验证 VFS Hook 是否正常工作，而非 fallthrough 到真实文件系统
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedFileChecker_ReadsVfsOnlyFile()
    {
        RequirePeTool();
        RequireHelper("FileChecker");

        var fcPath = TestExeBuilder.GetHelperPath("FileChecker");
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_vfs_only.enibox");

        // 使用一个磁盘上肯定不存在的路径
        var vfsOnlyPath = Path.Combine(Path.GetTempPath(), "EniBox_VFS_Only_File_" + Guid.NewGuid().ToString("N") + ".dat");
        var testContent = "This file only exists inside the VFS, not on disk!";

        // 创建临时源文件（用于封包时读取内容），但目标路径 vfsOnlyPath 不存在于磁盘
        var srcFile = TempFiles.CreateTempFile(testContent, ".tmp");

        var config = new PackConfiguration
        {
            SourceExePath = fcPath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true,
            EnableRegistryVirtualization = false
        };
        config.Files.Add(new PackFileItem
        {
            SourcePath = srcFile,
            VirtualPath = vfsOnlyPath,  // VFS 中存储的路径，磁盘上不存在此文件
            IsCompressed = false,
            OriginalSize = new FileInfo(srcFile).Length
        });

        var packResult = await PackService.PackAsync(config, null, CancellationToken.None);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");
        Logger.Success($"✓ 封包成功: {packResult.OutputFileSize} bytes");

        // 确认磁盘上确实不存在此文件
        Assert.False(File.Exists(vfsOnlyPath), "VFS-only 文件不应存在于磁盘上");

        // 运行封包后的 FileChecker，传入 VFS-only 路径
        var runResult = ProcessRunner.Run(outputPath, $"\"{vfsOnlyPath}\"", 15000);

        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出: {runResult.StandardOutput}");

        // FileChecker 应能通过 VFS Hook 成功打开并读取文件
        Assert.Equal(0, runResult.ExitCode);
        Assert.Contains("CHECK:FILE_READ:OK", runResult.StandardOutput);
        Assert.Contains("This file only exists inside the VFS", runResult.StandardOutput);
        Logger.Success("✓ 封包 FileChecker 成功读取 VFS-only 文件（磁盘上不存在）");
    }

    /// <summary>
    /// 验证: 封包本项目自有的 VfsTest.exe（C 原生程序），运行后验证
    /// VFS 文件读取、文件指针定位、文件属性查询、写入拒绝等全部功能正常。
    /// 这是最完整的端到端自举测试 — 覆盖 VFS 运行时的所有核心路径。
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedVfsTest_AllVfsFeatures()
    {
        RequirePeTool();
        RequireHelper("VfsTest");

        var vfsTestPath = TestExeBuilder.GetHelperPath("VfsTest");
        var outputDir = TempFiles.CreateTempDirectory();
        var outputPath = Path.Combine(outputDir, "vfstest.enibox");

        // 创建 VFS 数据文件：路径为 VfsTest.exe 硬编码的测试路径
        var vfsFilePath = @"C:\EniBox_VFS_Test_File.txt";
        var vfsContent = "Hello from EniBox VFS! This file is embedded at pack time.";
        var srcFile = TempFiles.CreateTempFile(vfsContent, ".tmp");

        // 封包 VfsTest.exe + 嵌入 VFS 文件
        var config = new PackConfiguration
        {
            SourceExePath = vfsTestPath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true,
            EnableRegistryVirtualization = false
        };
        config.Files.Add(new PackFileItem
        {
            SourcePath = srcFile,
            VirtualPath = vfsFilePath,
            IsCompressed = false,
            OriginalSize = new FileInfo(srcFile).Length
        });

        var packResult = await PackService.PackAsync(config, null, CancellationToken.None);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");
        Logger.Success($"✓ 封包成功: {packResult.OutputFileSize} bytes, VFS文件数: {config.Files.Count}");

        // 封包成功 = 输出文件必然存在，不存在说明写入链路有问题
        Assert.True(File.Exists(outputPath), "封包成功但输出文件不存在");

        // 运行封包后的 VfsTest.exe（无参数，它会运行所有内置测试）
        var runResult = ProcessRunner.Run(outputPath, "", 15000);

        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出:\n{runResult.StandardOutput}");

        // 核心验证：VFS 测试全部通过
        Assert.Contains("CHECK:RESULT:PASS", runResult.StandardOutput);
        Assert.Contains("CHECK:TEST_BEGIN:VfsTest", runResult.StandardOutput);

        // 逐项验证每个测试场景
        Assert.Contains("Test 1: VFS File Read", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_VfsFileRead", runResult.StandardOutput);

        Assert.Contains("Test 2: VFS File Seek", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_VfsFileSeek", runResult.StandardOutput);

        Assert.Contains("Test 3: VFS File Attributes", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_VfsFileAttributes", runResult.StandardOutput);

        Assert.Contains("Test 4: VFS File Not Found (passthrough)", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_VfsFileNotFound", runResult.StandardOutput);

        Assert.Contains("Test 5: VFS File Write Rejected", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_VfsFileWriteRejected", runResult.StandardOutput);

        Assert.Contains("Test 6: Child Process", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_ChildProcess", runResult.StandardOutput);

        // P0 安全加固覆盖 (MIT-231): Loader 提取路径 / CRC32 / 子进程注入策略
        Assert.Contains("Test 7: Loader Extraction Path (P0-02)", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_LoaderExtractionPath", runResult.StandardOutput);

        Assert.Contains("Test 8: Loader Integrity CRC32 (P0-02)", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_LoaderIntegrityCrc32", runResult.StandardOutput);

        Assert.Contains("Test 9: Sub-Process Injection Strategy (P0-03)", runResult.StandardOutput);
        Assert.Contains("CHECK:PASS:test_SubProcessInjectionStrategy", runResult.StandardOutput);

        // 确认没有任何 FAIL（用 "CHECK:FAIL:test" 前缀，避免误匹配汇总行 "CHECK:FAIL:0"）
        Assert.DoesNotContain("CHECK:FAIL:test", runResult.StandardOutput);

        Assert.Equal(0, runResult.ExitCode);
        Logger.Success("✓ 封包 VfsTest 全部 9 个测试通过，VFS 运行时 + P0 安全加固功能完整");
    }

    /// <summary>
    /// 验证: 封包后 fc.exe「真跑通」——给它两个内容不同的真实文件做比较，
    /// 断言退出码 1（fc.exe 的"文件不同"语义）且输出含 "*****" 比较块。
    /// 只有真跑通了文件比较，才证明 Loader hook 对原生系统二进制透明。
    /// 对应现有测试 PackedExeRunTests.E2E_PackedFcExe_RunsAndExitsNormally
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedFcExe_ExitsNormally_NotHanging()
    {
        RequirePeTool();

        var sourcePath = GetSystemFcExePath();
        var outputPath = Path.Combine(TempFiles.CreateTempDirectory(), "fc_exit.enibox");

        var packResult = await PackExeAsync(sourcePath, outputPath);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");
        Logger.Success($"✓ 封包成功: {packResult.OutputFileSize} bytes");
        Assert.True(File.Exists(outputPath), "封包成功但输出文件不存在");

        // 两个内容不同的真实文件：fc.exe 应实际执行比较并报告差异
        var file1 = TempFiles.CreateTempFile("hello", ".txt");
        var file2 = TempFiles.CreateTempFile("world", ".txt");

        var runResult = ProcessRunner.Run(outputPath, $"\"{file1}\" \"{file2}\"", 10000);

        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出: {runResult.StandardOutput}");

        Assert.False(runResult.TimedOut, "封包 fc.exe 不应超时挂起");
        Assert.Equal(1, runResult.ExitCode);
        Assert.Contains("*****", runResult.StandardOutput);
        Logger.Success("✓ 封包 fc.exe 真跑通：实际完成两文件比较并报告差异（exit=1）");
    }

    /// <summary>
    /// 验证: 子进程继承父 VFS — 封包 SubProcHost（携带 VFS-only 文件），由它启动
    /// 未封包的 SubProcChild。父的 Loader 注入子进程后经 VfsLink 握手继承父 VFS，
    /// 子进程读取仅存在于父 VFS 中的文件并回显内容。
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedSubProcHost_ChildInheritsParentVfs()
    {
        RequirePeTool();
        RequireHelper("SubProcHost");
        RequireHelper("SubProcChild");

        var hostPath = TestExeBuilder.GetHelperPath("SubProcHost");
        var childPath = TestExeBuilder.GetHelperPath("SubProcChild");
        var outputDir = TempFiles.CreateTempDirectory();
        var outputPath = Path.Combine(outputDir, "subproc.enibox");

        var vfsContent = "Hello from parent VFS to the child process!";
        var srcFile = TempFiles.CreateTempFile(vfsContent, ".txt");
        var vfsPath = @"C:\EniBox_child_vfs_e2e.txt";

        var config = new PackConfiguration
        {
            SourceExePath = hostPath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true,
            EnableRegistryVirtualization = false
        };
        config.Files.Add(new PackFileItem
        {
            SourcePath = srcFile,
            VirtualPath = vfsPath,
            IsCompressed = false,
            OriginalSize = new FileInfo(srcFile).Length
        });

        var packResult = await PackService.PackAsync(config, null, CancellationToken.None);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");

        var runResult = ProcessRunner.Run(outputPath, $"\"{childPath}\" \"{vfsPath}\"", 20000);

        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出:\n{runResult.StandardOutput}");

        Assert.False(runResult.TimedOut, "不应超时");
        // host 会用 CreateProcessA 与 CreateProcessW 各启动一次子进程，
        // 覆盖两条注入路径；每次都应 SUBPROC_CREATE:OK + CHILD_VFS:OK
        Assert.Equal(2, runResult.StandardOutput.Split("CHECK:SUBPROC_CREATE:OK").Length - 1);
        Assert.Equal(2, runResult.StandardOutput.Split("CHECK:CHILD_VFS:OK").Length - 1);
        Assert.Contains(vfsContent, runResult.StandardOutput);
        Logger.Success("✓ 子进程经 VfsLink 继承父 VFS（A/W 两族注入路径均读到 VFS-only 文件）");
    }

    /// <summary>
    /// 验证: EnableSubProcessInjection=false 时子进程不注入、不继承 VFS。
    /// 配置位经 .enibox 引导区 [272..275] 传递给 Loader，关掉后打包程序
    /// 启动的子进程走真实文件系统（读 VFS-only 路径应 GLE=2）。
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedSubProcHost_InjectionDisabled_ChildDoesNotInheritVfs()
    {
        RequirePeTool();
        RequireHelper("SubProcHost");
        RequireHelper("SubProcChild");

        var hostPath = TestExeBuilder.GetHelperPath("SubProcHost");
        var childPath = TestExeBuilder.GetHelperPath("SubProcChild");
        var outputDir = TempFiles.CreateTempDirectory();
        var outputPath = Path.Combine(outputDir, "subproc_noinject.enibox");

        var vfsContent = "This must NOT reach the child when injection is off.";
        var srcFile = TempFiles.CreateTempFile(vfsContent, ".txt");
        var vfsPath = @"C:\EniBox_child_vfs_off.txt";

        var config = new PackConfiguration
        {
            SourceExePath = hostPath,
            OutputPath = outputPath,
            EnableSubProcessInjection = false,   // 开关关闭
            EnableRegistryVirtualization = false
        };
        config.Files.Add(new PackFileItem
        {
            SourcePath = srcFile,
            VirtualPath = vfsPath,
            IsCompressed = false,
            OriginalSize = new FileInfo(srcFile).Length
        });

        var packResult = await PackService.PackAsync(config, null, CancellationToken.None);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");

        var runResult = ProcessRunner.Run(outputPath, $"\"{childPath}\" \"{vfsPath}\"", 20000);

        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出:\n{runResult.StandardOutput}");

        Assert.False(runResult.TimedOut, "不应超时");
        // 两个子进程（A/W 族）都被创建，但都不该读到 VFS 内容
        Assert.Equal(2, runResult.StandardOutput.Split("CHECK:SUBPROC_CREATE:OK").Length - 1);
        Assert.DoesNotContain("CHECK:CHILD_VFS:OK", runResult.StandardOutput);
        Assert.Equal(2, runResult.StandardOutput.Split("CHECK:CHILD_VFS:FAIL").Length - 1);
        Assert.DoesNotContain(vfsContent, runResult.StandardOutput);
        Logger.Success("✓ 子进程注入关闭：子进程未继承 VFS（走真实文件系统）");
    }

    /// <summary>
    /// 验证: 注册表虚拟化（实验性）— 封包 RegChecker 时预置
    /// HKCU\Software\EniBoxTest\TestValue（该键真实注册表中不存在），
    /// 封包程序内读取应命中虚拟注册表；真实注册表读取不受影响。
    /// </summary>
[SkippableFact]
    public async Task E2E_PackedRegChecker_VirtualRegistryPreset()
    {
        RequirePeTool();
        RequireHelper("RegChecker");

        var regCheckerPath = TestExeBuilder.GetHelperPath("RegChecker");
        var outputDir = TempFiles.CreateTempDirectory();
        var outputPath = Path.Combine(outputDir, "regchecker.enibox");

        var presetValue = "preset-registry-20260915";
        var config = new PackConfiguration
        {
            SourceExePath = regCheckerPath,
            OutputPath = outputPath,
            EnableSubProcessInjection = true,
            EnableRegistryVirtualization = true
        };
        config.RegistryValues.Add(PackRegistryValue.FromString(
            @"HKEY_CURRENT_USER\Software\EniBoxTest", "TestValue", presetValue));

        var packResult = await PackService.PackAsync(config, null, CancellationToken.None);
        Assert.True(packResult.IsSuccess, $"封包失败: {packResult.ErrorMessage}");

        var runResult = ProcessRunner.Run(outputPath, "", 15000);
        Logger.Info($"退出码: {runResult.ExitCode}");
        Logger.Info($"输出:\n{runResult.StandardOutput}");

        Assert.False(runResult.TimedOut, "不应超时");
        Assert.Contains("CHECK:REG_VIRTUAL:OK", runResult.StandardOutput);
        Assert.Contains(presetValue, runResult.StandardOutput);
        Assert.Contains("CHECK:REG_REAL:OK", runResult.StandardOutput);
        Logger.Success("✓ 注册表虚拟化：预置值在封包程序内可读，真实注册表不受影响");
    }
}
