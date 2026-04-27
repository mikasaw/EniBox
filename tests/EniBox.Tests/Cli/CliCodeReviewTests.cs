using System;
using System.IO;
using System.Reflection;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Cli;

public class CliCodeReviewTests : VerificationTestBase
{
    public CliCodeReviewTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void T4_01_Review_CliEntryPointExists()
    {
        Logger.Info("开始审查：CLI入口点是否存在");
        
        var guiProjectDir = FileHelper.GetProjectDir("EniBox.GUI");
        var programFile = Path.Combine(guiProjectDir, "Program.cs");
        
        Assert.True(File.Exists(programFile), "Program.cs应存在");
        Logger.Success($"✓ 入口文件存在: {programFile}");
        
        var programSource = File.ReadAllText(programFile);
        bool hasMainMethod = programSource.Contains("static int Main");
        
        Assert.True(hasMainMethod, "Program.cs应包含Main方法");
        Logger.Success("✓ Main方法存在");
        
        bool hasStatThread = programSource.Contains("[STAThread]");
        Assert.True(hasStatThread, "Program.cs应包含[STAThread]属性");
        Logger.Success("✓ [STAThread]属性存在");
    }
    
    [Fact]
    public void T4_01_Review_CliParameters()
    {
        Logger.Info("开始审查：CLI参数定义");
        
        var guiProjectDir = FileHelper.GetProjectDir("EniBox.GUI");
        var cliRunnerFile = Path.Combine(guiProjectDir, "Services", "CliRunner.cs");
        
        Assert.True(File.Exists(cliRunnerFile), "CliRunner.cs应存在");
        Logger.Success($"✓ CliRunner文件存在: {cliRunnerFile}");
        
        var cliRunnerSource = File.ReadAllText(cliRunnerFile);
        
        var requiredParams = new[]
        {
            "--source",
            "--output",
            "--files",
            "--compress"
        };
        
        int paramCount = 0;
        foreach (var param in requiredParams)
        {
            if (cliRunnerSource.Contains(param))
            {
                Logger.Success($"✓ 参数定义: {param}");
                paramCount++;
            }
            else
            {
                Logger.Fail($"✗ 参数未定义: {param}");
            }
        }
        
        Assert.Equal(requiredParams.Length, paramCount);
        Logger.Info($"审查完成：{paramCount}/{requiredParams.Length} 参数已定义");
    }
    
    [Fact]
    public void T4_01_Review_SystemCommandLineUsage()
    {
        Logger.Info("开始审查：System.CommandLine库使用");
        
        var csprojFile = File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.GUI") + "/EniBox.GUI.csproj");
        
        bool hasCommandLinePackage = csprojFile.Contains("System.CommandLine");
        Assert.True(hasCommandLinePackage, "应引用System.CommandLine包");
        Logger.Success("✓ System.CommandLine包已引用");
    }
}
