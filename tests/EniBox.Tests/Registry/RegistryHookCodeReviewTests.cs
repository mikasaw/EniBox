using System;
using System.Reflection;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Registry;

public class RegistryHookCodeReviewTests : VerificationTestBase
{
    public RegistryHookCodeReviewTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void T2_01_Review_RegistryHookFilesExist()
    {
        Logger.Info("开始审查：注册表Hook源文件是否存在");
        
        var loaderSrcDir = FileHelper.GetProjectDir("EniBox.Loader") + "/src";
        var includeDir = FileHelper.GetProjectDir("EniBox.Loader") + "/include";
        
        var requiredFiles = new[]
        {
            $"{loaderSrcDir}/hook_registry.c",
            $"{includeDir}/hook_registry.h"
        };
        
        int foundCount = 0;
        foreach (var file in requiredFiles)
        {
            if (System.IO.File.Exists(file))
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
    public void T2_01_Review_HookFunctionsDefined()
    {
        Logger.Info("开始审查：Hook函数是否定义");
        
        var hookSource = System.IO.File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/src/hook_registry.c");
        
        var requiredHooks = new[]
        {
            "Hook_RegOpenKeyExA",
            "Hook_RegOpenKeyExW",
            "Hook_RegQueryValueExA",
            "Hook_RegQueryValueExW",
            "Hook_RegCloseKey",
            "Hook_RegEnumValueA",
            "Hook_RegEnumValueW",
            "Hook_RegSetValueExA",
            "Hook_RegSetValueExW"
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
    public void T2_01_Review_VirtualRegistryStructures()
    {
        Logger.Info("开始审查：虚拟注册表数据结构");
        
        var headerSource = System.IO.File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/include/hook_registry.h");
        
        var requiredStructures = new[]
        {
            "VREG_VALUE",
            "VREG_KEY",
            "VREG_HANDLE",
            "MAX_VREG_HANDLES",
            "VREG_HANDLE_BASE"
        };
        
        int structCount = 0;
        foreach (var structure in requiredStructures)
        {
            if (headerSource.Contains(structure))
            {
                Logger.Success($"✓ 数据结构定义: {structure}");
                structCount++;
            }
            else
            {
                Logger.Fail($"✗ 数据结构未定义: {structure}");
            }
        }
        
        Assert.Equal(requiredStructures.Length, structCount);
        Logger.Info($"审查完成：{structCount}/{requiredStructures.Length} 数据结构已定义");
    }
    
    [Fact]
    public void T2_01_Review_InitializationFunctions()
    {
        Logger.Info("开始审查：初始化和清理函数");
        
        var source = System.IO.File.ReadAllText(
            FileHelper.GetProjectDir("EniBox.Loader") + "/src/hook_registry.c");
        
        var requiredFunctions = new[]
        {
            "VReg_Initialize",
            "VReg_Finalize",
            "VReg_FindKeyA",
            "VReg_FindKeyW",
            "VReg_IsVirtualKeyA",
            "VReg_IsVirtualKeyW",
            "VReg_GetHandle",
            "VReg_AllocHandle",
            "VReg_FreeHandle",
            "VReg_IsVirtualHandle",
            "HookRegistry_Install"
        };
        
        int funcCount = 0;
        foreach (var func in requiredFunctions)
        {
            if (source.Contains(func))
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
}
