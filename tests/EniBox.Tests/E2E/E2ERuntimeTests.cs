using System;
using System.IO;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.E2E;

public class E2EVfsRuntimeTests : E2ETestBase
{
    public E2EVfsRuntimeTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void E2E_FileChecker_ReadsExistingFile_ViaWin32()
    {
        if (!TestExeBuilder.IsHelperAvailable("FileChecker"))
        {
            Logger.Warning("⚠ FileChecker未编译，跳过测试");
            return;
        }
        
        var fileChecker = TestExeBuilder.GetHelperPath("FileChecker");
        var testFile = TempFiles.CreateTempFile("Hello from EniBox VFS test!", ".txt");
        
        var result = ProcessRunner.Run(fileChecker, $"\"{testFile}\"", 10000);
        
        Logger.Info($"退出码: {result.ExitCode}");
        Logger.Info($"输出: {result.StandardOutput}");
        
        Assert.Equal(0, result.ExitCode);
        Assert.Contains("CHECK:FILE_READ:OK", result.StandardOutput);
        Logger.Success("✓ FileChecker通过Win32 API成功读取文件");
    }
    
    [Fact]
    public void E2E_FileChecker_NonExistentFile_ReturnsError()
    {
        if (!TestExeBuilder.IsHelperAvailable("FileChecker"))
        {
            Logger.Warning("⚠ FileChecker未编译，跳过测试");
            return;
        }
        
        var fileChecker = TestExeBuilder.GetHelperPath("FileChecker");
        var result = ProcessRunner.Run(fileChecker, "\"C:\\nonexistent_file_12345.txt\"", 10000);
        
        Logger.Info($"退出码: {result.ExitCode}");
        Logger.Info($"输出: {result.StandardOutput}");
        
        Assert.NotEqual(0, result.ExitCode);
        Assert.Contains("CHECK:FILE_OPEN:FAIL", result.StandardOutput);
        Logger.Success("✓ FileChecker正确报告文件不存在");
    }
}

public class E2ERegVirtRuntimeTests : E2ETestBase
{
    public E2ERegVirtRuntimeTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void E2E_RegChecker_ReadsRealRegistry_Succeeds()
    {
        if (!TestExeBuilder.IsHelperAvailable("RegChecker"))
        {
            Logger.Warning("⚠ RegChecker未编译，跳过测试");
            return;
        }
        
        var regChecker = TestExeBuilder.GetHelperPath("RegChecker");
        var result = ProcessRunner.Run(regChecker, "", 10000);
        
        Logger.Info($"退出码: {result.ExitCode}");
        Logger.Info($"输出: {result.StandardOutput}");
        
        Assert.Contains("CHECK:REG_REAL:OK", result.StandardOutput);
        Logger.Success("✓ RegChecker成功读取真实注册表(ProductName)");
    }
    
    [Fact]
    public void E2E_RegChecker_VirtualKey_NotFoundWithoutVfs()
    {
        if (!TestExeBuilder.IsHelperAvailable("RegChecker"))
        {
            Logger.Warning("⚠ RegChecker未编译，跳过测试");
            return;
        }
        
        var regChecker = TestExeBuilder.GetHelperPath("RegChecker");
        var result = ProcessRunner.Run(regChecker, "", 10000);
        
        Logger.Info($"输出: {result.StandardOutput}");
        
        Assert.Contains("CHECK:REG_VIRTUAL:FAIL", result.StandardOutput);
        Logger.Success("✓ 无VFS时虚拟注册表键不存在(预期行为)");
    }
}

public class E2ESubProcRuntimeTests : E2ETestBase
{
    public E2ESubProcRuntimeTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void E2E_SubProcHost_LaunchesChild_Succeeds()
    {
        if (!TestExeBuilder.IsHelperAvailable("SubProcHost") || 
            !TestExeBuilder.IsHelperAvailable("SubProcChild"))
        {
            Logger.Warning("⚠ SubProcHost/Child未编译，跳过测试");
            return;
        }
        
        var host = TestExeBuilder.GetHelperPath("SubProcHost");
        var child = TestExeBuilder.GetHelperPath("SubProcChild");
        
        var result = ProcessRunner.Run(host, $"\"{child}\"", 15000);
        
        Logger.Info($"退出码: {result.ExitCode}");
        Logger.Info($"输出: {result.StandardOutput}");
        
        Assert.Contains("CHECK:SUBPROC_CREATE:OK", result.StandardOutput);
        Assert.Contains("CHECK:SUBPROC_EXIT:OK", result.StandardOutput);
        Logger.Success("✓ SubProcHost成功启动子进程");
    }
}
