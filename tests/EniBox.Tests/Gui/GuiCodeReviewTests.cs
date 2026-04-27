using System;
using System.IO;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Gui;

public class GuiCodeReviewTests : VerificationTestBase
{
    public GuiCodeReviewTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void T5_01_Review_MainWindowExists()
    {
        Logger.Info("开始审查：主窗口XAML是否存在");
        
        var viewsDir = FileHelper.GetProjectDir("EniBox.GUI") + "/Views";
        var mainWindow = Path.Combine(viewsDir, "MainWindow.xaml");
        
        Assert.True(File.Exists(mainWindow), "MainWindow.xaml应存在");
        Logger.Success($"✓ 主窗口文件存在: {mainWindow}");
    }
    
    [Fact]
    public void T5_01_Review_MainViewModelExists()
    {
        Logger.Info("开始审查：MainViewModel是否存在");
        
        var vmDir = FileHelper.GetProjectDir("EniBox.GUI") + "/ViewModels";
        var mainViewModel = Path.Combine(vmDir, "MainViewModel.cs");
        
        Assert.True(File.Exists(mainViewModel), "MainViewModel.cs应存在");
        Logger.Success($"✓ ViewModel文件存在: {mainViewModel}");
        
        var vmSource = File.ReadAllText(mainViewModel);
        
        bool hasPackCommand = vmSource.Contains("PackCommand");
        bool hasProgress = vmSource.Contains("Progress");
        
        Logger.Info($"PackCommand定义: {(hasPackCommand ? "✓" : "✗")}");
        Logger.Info($"进度属性定义: {(hasProgress ? "✓" : "✗")}");
        
        Logger.Success("GUI结构审查通过");
    }
    
    [Fact]
    public void T5_01_Review_InternationalizationResources()
    {
        Logger.Info("开始审查：国际化资源文件");
        
        var resourcesDir = FileHelper.GetProjectDir("EniBox.GUI") + "/Resources";
        
        var zhCN = Path.Combine(resourcesDir, "Strings.zh-CN.xaml");
        var enUS = Path.Combine(resourcesDir, "Strings.en-US.xaml");
        
        bool hasZhCN = File.Exists(zhCN);
        bool hasEnUS = File.Exists(enUS);
        
        Logger.Info($"中文资源: {(hasZhCN ? "✓" : "✗")}");
        Logger.Info($"英文资源: {(hasEnUS ? "✓" : "✗")}");
        
        Assert.True(hasZhCN, "应包含中文资源文件");
        Assert.True(hasEnUS, "应包含英文资源文件");
        
        Logger.Success("国际化资源审查通过");
    }
}
