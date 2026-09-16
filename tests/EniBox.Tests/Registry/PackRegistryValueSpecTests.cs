using System;
using EniBox.GUI.Models;
using Xunit;

namespace EniBox.Tests.Registry;

/// <summary>
/// CLI --registry-value 规格解析（PackRegistryValue.FromSpec）单元测试：
/// 四段/三段格式、四种类型、非法输入报错。
/// </summary>
public class PackRegistryValueSpecTests
{
    private const string Key = @"HKEY_CURRENT_USER\Software\App";

    [Fact]
    public void FromSpec_FourSegment_SZ()
    {
        var v = PackRegistryValue.FromSpec($"{Key}|TestValue|SZ|hello");
        Assert.Equal(Key, v.KeyPath);
        Assert.Equal("TestValue", v.ValueName);
        Assert.Equal(PackRegistryValue.REG_SZ, v.Type);
        var expected = System.Text.Encoding.Unicode.GetBytes("hello\0");
        Assert.Equal(expected, v.Data);
    }

    [Fact]
    public void FromSpec_ThreeSegment_DefaultsToSZ()
    {
        var v = PackRegistryValue.FromSpec($"{Key}|TestValue|hello");
        Assert.Equal(PackRegistryValue.REG_SZ, v.Type);
        Assert.Equal("hello", System.Text.Encoding.Unicode.GetString(v.Data).TrimEnd('\0'));
    }

    [Fact]
    public void FromSpec_ExpandString()
    {
        var v = PackRegistryValue.FromSpec($"{Key}|Path|EXPAND_SZ|%SystemRoot%\\x");
        Assert.Equal(PackRegistryValue.REG_EXPAND_SZ, v.Type);
        Assert.Contains("%SystemRoot%", System.Text.Encoding.Unicode.GetString(v.Data));
    }

    [Fact]
    public void FromSpec_Dword()
    {
        var v = PackRegistryValue.FromSpec($"{Key}|Count|DWORD|4294967295");
        Assert.Equal(PackRegistryValue.REG_DWORD, v.Type);
        Assert.Equal(4, v.Data.Length);
        Assert.Equal(0xFFFFFFFFu, BitConverter.ToUInt32(v.Data, 0));
    }

    [Fact]
    public void FromSpec_Binary_WithSpaces()
    {
        var v = PackRegistryValue.FromSpec($"{Key}|Blob|BINARY|DE AD BE EF");
        Assert.Equal(PackRegistryValue.REG_BINARY, v.Type);
        Assert.Equal(new byte[] { 0xDE, 0xAD, 0xBE, 0xEF }, v.Data);
    }

    [Theory]
    [InlineData("KEY|NAME")]                       // 段数不足
    [InlineData("KEY|NAME|SZ|DATA|EXTRA")]         // 段数过多
    [InlineData("|NAME|SZ|data")]                  // KEY 为空
    [InlineData("KEY|NAME|FOO|data")]              // 类型未知
    [InlineData("KEY|NAME|DWORD|abc")]             // DWORD 非数字
    [InlineData("KEY|NAME|BINARY|XYZ")]            // BINARY 非十六进制
    [InlineData("KEY|NAME|BINARY|ABC")]            // BINARY 奇数长度
    public void FromSpec_Invalid_ThrowsArgumentException(string spec)
    {
        Assert.Throws<ArgumentException>(() => PackRegistryValue.FromSpec(spec));
    }

    [Fact]
    public void FromSpec_EmptyValueName_DefaultValue()
    {
        // 默认值（名称为空串）合法：KEY||SZ|data
        var v = PackRegistryValue.FromSpec($"{Key}||SZ|default");
        Assert.Equal("", v.ValueName);
        Assert.Equal(PackRegistryValue.REG_SZ, v.Type);
    }
}
