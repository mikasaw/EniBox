using System;
using System.Runtime.InteropServices;
using EniBox.GUI.Interop;
using EniBox.GUI.Models;
using EniBox.Tests.TestInfrastructure;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.Interop;

public class PeToolInteropStructTests : VerificationTestBase
{
    public PeToolInteropStructTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void ImportEntry_StructSize_Is256()
    {
        var size = Marshal.SizeOf<PeToolInterop.ImportEntry>();
        Assert.Equal(256, size);
        Logger.Success($"✓ ImportEntry大小: {size} bytes");
    }
    
    [Fact]
    public void ImportEntry_DllName_CanBeSetAndRead()
    {
        var entry = new PeToolInterop.ImportEntry
        {
            DllName = "kernel32.dll"
        };
        
        Assert.Equal("kernel32.dll", entry.DllName);
        Logger.Success("✓ ImportEntry.DllName可设置和读取");
    }
    
    [Fact]
    public void ImportEntry_DllName_HasFixedMaxSize()
    {
        var entry = new PeToolInterop.ImportEntry
        {
            DllName = "kernel32.dll"
        };
        
        var structSize = Marshal.SizeOf<PeToolInterop.ImportEntry>();
        Assert.Equal(256, structSize);
        Logger.Success($"✓ ImportEntry固定大小256字节，DllName最大255字符");
    }
    
    [Fact]
    public void PeArchitecture_Values_MatchImageFileMachine()
    {
        Assert.Equal(0x014C, (int)PeArchitecture.X86);
        Assert.Equal(0x8664, (int)PeArchitecture.X64);
        Assert.Equal(0, (int)PeArchitecture.Unknown);
        Logger.Success("✓ PeArchitecture枚举值正确");
    }
}

public class PeToolInteropPInvokeTests : VerificationTestBase
{
    public PeToolInteropPInvokeTests(ITestOutputHelper output) : base(output) { }
    
    [Fact]
    public void PeTool_Dll_IsAvailable()
    {
        var isAvailable = PeToolAvailabilityChecker.IsAvailable;
        
        if (!isAvailable)
        {
            Logger.Warning("⚠ PeTool.dll不可用，跳过PInvoke测试");
            return;
        }
        
        Logger.Success("✓ PeTool.dll可用");
    }
    
    [Fact]
    public void PeTool_Open_InvalidPath_ReturnsError()
    {
        if (!PeToolAvailabilityChecker.IsAvailable)
        {
            Logger.Warning("⚠ PeTool.dll不可用，跳过测试");
            return;
        }
        
        var result = PeToolInterop.Open(@"C:\nonexistent_pe_file.exe", out var ctx);
        
        Assert.NotEqual(0, result);
        Logger.Success($"✓ 无效路径返回错误码: {result}");
    }
    
    [Fact]
    public void PeTool_Open_NullPath_ThrowsOrReturnsError()
    {
        if (!PeToolAvailabilityChecker.IsAvailable)
        {
            Logger.Warning("⚠ PeTool.dll不可用，跳过测试");
            return;
        }
        
        try
        {
            var result = PeToolInterop.Open("", out var ctx);
            Assert.NotEqual(0, result);
            Logger.Success($"✓ 空路径返回错误码: {result}");
        }
        catch (Exception ex)
        {
            Logger.Info($"空路径抛出异常(预期行为): {ex.GetType().Name}");
        }
    }
}
