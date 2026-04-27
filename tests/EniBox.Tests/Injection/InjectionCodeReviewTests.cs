using System;
using System.IO;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Injection;

public class InjectionCodeReviewTests : VerificationTestBase
{
    public InjectionCodeReviewTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void T3_01_Review_InjectionFilesExist()
    {
        Logger.Info("开始审查：子进程注入源文件是否存在");
        
        var loaderSrcDir = FileHelper.GetProjectDir("EniBox.Loader") + "/src";
        var includeDir = FileHelper.GetProjectDir("EniBox.Loader") + "/include";
        
        var requiredFiles = new[]
        {
            $"{loaderSrcDir}/hook_process.c",
            $"{loaderSrcDir}/inject.c",
            $"{includeDir}/hook_process.h",
            $"{includeDir}/inject.h"
        };
        
        int foundCount = 0;
        foreach (var file in requiredFiles)
        {
            if (File.Exists(file))
            {
                Logger.Success($"✓ 文件存在: {file}");
                foundCount++;
            }
            else
            {
                Logger.Fail($"✗ 文件缺失: {file}");
            }
        }
        
        Assert.Equal(requiredFiles.Length, foundCount);
        Logger.Info($"审查完成：{foundCount}/{requiredFiles.Length} 文件存在");
    }
    
    [Fact]
    public void T3_01_Review_CreateProcessHooksDefined()
    {
        Logger.Info("开始审查：CreateProcess Hook函数是否定义");
        
        var hookSource = File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/src/hook_process.c");
        
        var requiredHooks = new[]
        {
            "Hook_CreateProcessW",
            "Hook_CreateProcessA",
            "HookProcess_SetLoaderPath",
            "HookProcess_GetLoaderPath"
        };
        
        int definedCount = 0;
        foreach (var hook in requiredHooks)
        {
            if (hookSource.Contains(hook))
            {
                Logger.Success($"✓ Hook函数定义: {hook}");
                definedCount++;
            }
            else
            {
                Logger.Fail($"✗ Hook函数未定义: {hook}");
            }
        }
        
        Assert.Equal(requiredHooks.Length, definedCount);
        Logger.Info($"审查完成：{definedCount}/{requiredHooks.Length} Hook函数已定义");
    }
    
    [Fact]
    public void T3_01_Review_InjectionFunctionsDefined()
    {
        Logger.Info("开始审查：DLL注入函数是否定义");
        
        var injectSource = File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/src/inject.c");
        
        var requiredFunctions = new[]
        {
            "Inject_LoadDll",
            "Inject_ArchitectureMatches"
        };
        
        int funcCount = 0;
        foreach (var func in requiredFunctions)
        {
            if (injectSource.Contains(func))
            {
                Logger.Success($"✓ 函数定义: {func}");
                funcCount++;
            }
            else
            {
                Logger.Fail($"✗ 函数未定义: {func}");
            }
        }
        
        Assert.Equal(requiredFunctions.Length, funcCount);
        Logger.Info($"审查完成：{funcCount}/{requiredFunctions.Length} 函数已定义");
    }
    
    [Fact]
    public void T3_01_Review_ArchitectureDetectionLogic()
    {
        Logger.Info("开始审查：架构检测逻辑");
        
        var injectSource = File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/src/inject.c");
        
        bool hasIsWow64Process2 = injectSource.Contains("IsWow64Process2");
        bool hasIsWow64Process = injectSource.Contains("IsWow64Process");
        bool hasFallback = injectSource.Contains("is_current_64bit");
        
        Logger.Info($"IsWow64Process2检测: {(hasIsWow64Process2 ? "✓" : "✗")}");
        Logger.Info($"IsWow64Process回退: {(hasIsWow64Process ? "✓" : "✗")}");
        Logger.Info($"简单比较回退: {(hasFallback ? "✓" : "✗")}");
        
        Assert.True(hasIsWow64Process2, "应包含IsWow64Process2精确检测");
        Assert.True(hasIsWow64Process, "应包含IsWow64Process回退");
        Assert.True(hasFallback, "应包含简单比较回退");
        
        Logger.Success("架构检测逻辑审查通过");
    }
    
    [Fact]
    public void T3_01_Review_SuspendedProcessCreation()
    {
        Logger.Info("开始审查：CREATE_SUSPENDED标志使用");
        
        var hookSource = File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/src/hook_process.c");
        
        bool hasSuspendedFlag = hookSource.Contains("CREATE_SUSPENDED");
        bool hasResumeThread = hookSource.Contains("ResumeThread");
        
        Logger.Info($"CREATE_SUSPENDED标志: {(hasSuspendedFlag ? "✓" : "✗")}");
        Logger.Info($"ResumeThread恢复: {(hasResumeThread ? "✓" : "✗")}");
        
        Assert.True(hasSuspendedFlag, "应使用CREATE_SUSPENDED创建子进程");
        Assert.True(hasResumeThread, "注入后应调用ResumeThread恢复");
        
        Logger.Success("挂起进程创建逻辑审查通过");
    }
}
