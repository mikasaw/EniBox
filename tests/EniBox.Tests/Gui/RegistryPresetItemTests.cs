using System;
using EniBox.GUI.Models;
using EniBox.GUI.ViewModels;
using Xunit;

namespace EniBox.Tests.Gui;

/// <summary>
/// GUI 注册表预置值编辑行（RegistryPresetItem）单元测试：类型映射与非法输入。
/// </summary>
public class RegistryPresetItemTests
{
    private const string Key = @"HKEY_CURRENT_USER\Software\MyApp";

    [Fact]
    public void ToPackValue_SZ_Default()
    {
        var item = new RegistryPresetItem { KeyPath = Key, ValueName = "Dir", Type = "SZ", Data = @"C:\Apps" };
        var v = item.ToPackValue();
        Assert.Equal(PackRegistryValue.REG_SZ, v.Type);
        Assert.Equal(Key, v.KeyPath);
        Assert.Equal("Dir", v.ValueName);
    }

    [Fact]
    public void ToPackValue_Dword()
    {
        var item = new RegistryPresetItem { KeyPath = Key, ValueName = "Count", Type = "DWORD", Data = "7" };
        Assert.Equal(PackRegistryValue.REG_DWORD, item.ToPackValue().Type);
    }

    [Fact]
    public void ToPackValue_Binary()
    {
        var item = new RegistryPresetItem { KeyPath = Key, ValueName = "Blob", Type = "BINARY", Data = "DE AD" };
        var v = item.ToPackValue();
        Assert.Equal(PackRegistryValue.REG_BINARY, v.Type);
        Assert.Equal(new byte[] { 0xDE, 0xAD }, v.Data);
    }

    [Fact]
    public void ToPackValue_InvalidType_Throws()
    {
        var item = new RegistryPresetItem { KeyPath = Key, ValueName = "X", Type = "FOO", Data = "1" };
        Assert.Throws<ArgumentException>(item.ToPackValue);
    }

    [Fact]
    public void ToPackValue_EmptyKey_Throws()
    {
        var item = new RegistryPresetItem { KeyPath = "", ValueName = "X", Type = "SZ", Data = "1" };
        Assert.Throws<ArgumentException>(item.ToPackValue);
    }

    [Fact]
    public void ToPackValue_DwordNotNumeric_Throws()
    {
        var item = new RegistryPresetItem { KeyPath = Key, ValueName = "N", Type = "DWORD", Data = "abc" };
        Assert.Throws<ArgumentException>(item.ToPackValue);
    }
}
