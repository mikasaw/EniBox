using System;
using System.Collections.Generic;
using EniBox.GUI.Models;
using Xunit;

namespace EniBox.Tests.Config;

public class PackConfigurationTests
{
    [Fact]
    public void DefaultValues_AreCorrect()
    {
        var config = new PackConfiguration();
        
        Assert.Equal(string.Empty, config.SourceExePath);
        Assert.Equal(string.Empty, config.OutputPath);
        Assert.Empty(config.Files);
        Assert.False(config.EnableRegistryVirtualization);
        Assert.True(config.EnableSubProcessInjection);
    }
    
    [Fact]
    public void Files_IsMutableList()
    {
        var config = new PackConfiguration();
        var item = new PackFileItem { SourcePath = "test.dll", OriginalSize = 1024 };
        
        config.Files.Add(item);
        
        Assert.Single(config.Files);
        Assert.Equal("test.dll", config.Files[0].SourcePath);
    }
    
    [Fact]
    public void Properties_CanBeSet()
    {
        var config = new PackConfiguration
        {
            SourceExePath = @"C:\app.exe",
            OutputPath = @"C:\output.exe",
            EnableRegistryVirtualization = true,
            EnableSubProcessInjection = false
        };
        
        Assert.Equal(@"C:\app.exe", config.SourceExePath);
        Assert.Equal(@"C:\output.exe", config.OutputPath);
        Assert.True(config.EnableRegistryVirtualization);
        Assert.False(config.EnableSubProcessInjection);
    }
}

public class PackResultTests
{
    [Fact]
    public void SuccessResult_HasCorrectProperties()
    {
        var result = new PackResult
        {
            IsSuccess = true,
            OutputPath = @"C:\output.exe",
            OutputFileSize = 12345,
            ErrorCode = 0
        };
        
        Assert.True(result.IsSuccess);
        Assert.Equal(@"C:\output.exe", result.OutputPath);
        Assert.Equal(12345, result.OutputFileSize);
        Assert.Equal(0, result.ErrorCode);
        Assert.Equal(string.Empty, result.ErrorMessage);
    }
    
    [Fact]
    public void FailureResult_HasErrorCode()
    {
        var result = new PackResult
        {
            IsSuccess = false,
            ErrorCode = PackErrorCode.FileNotFound,
            ErrorMessage = "Source file not found"
        };
        
        Assert.False(result.IsSuccess);
        Assert.Equal(PackErrorCode.FileNotFound, result.ErrorCode);
        Assert.Equal("Source file not found", result.ErrorMessage);
    }
}

public class PackErrorCodeTests
{
    [Fact]
    public void AllErrorCodes_AreInValidRanges()
    {
        var codes = new List<(int Code, int Min, int Max, string Name)>
        {
            (PackErrorCode.FileNotFound, 1000, 1999, nameof(PackErrorCode.FileNotFound)),
            (PackErrorCode.InvalidPe, 2000, 2999, nameof(PackErrorCode.InvalidPe)),
            (PackErrorCode.UnsupportedArch, 2000, 2999, nameof(PackErrorCode.UnsupportedArch)),
            (PackErrorCode.VfsBuildFailed, 3000, 3999, nameof(PackErrorCode.VfsBuildFailed)),
            (PackErrorCode.CompressionFailed, 4000, 4999, nameof(PackErrorCode.CompressionFailed)),
            (PackErrorCode.WriteFailed, 5000, 5999, nameof(PackErrorCode.WriteFailed)),
            (PackErrorCode.LoaderNotFound, 6000, 6999, nameof(PackErrorCode.LoaderNotFound)),
            (PackErrorCode.OperationCancelled, 9000, 9999, nameof(PackErrorCode.OperationCancelled)),
        };
        
        foreach (var (code, min, max, name) in codes)
        {
            Assert.True(code >= min && code <= max, $"{name}={code} 不在范围 [{min},{max}] 内");
        }
    }
    
    [Fact]
    public void AllErrorCodes_AreUnique()
    {
        var codeFields = typeof(PackErrorCode).GetFields(
            System.Reflection.BindingFlags.Public | 
            System.Reflection.BindingFlags.Static);
        
        var seen = new HashSet<int>();
        foreach (var field in codeFields)
        {
            var value = (int)field.GetValue(null)!;
            Assert.True(seen.Add(value), $"重复的错误码: {field.Name}={value}");
        }
    }
}

public class PeArchitectureTests
{
    [Fact]
    public void EnumValues_MatchImageFileMachine()
    {
        Assert.Equal(0, (int)PeArchitecture.Unknown);
        Assert.Equal(0x014C, (int)PeArchitecture.X86);
        Assert.Equal(0x8664, (int)PeArchitecture.X64);
    }
}

public class PeInfoTests
{
    [Fact]
    public void Is64Bit_ReturnsTrueForX64()
    {
        var info = new PeInfo { Architecture = PeArchitecture.X64 };
        Assert.True(info.Is64Bit);
    }
    
    [Fact]
    public void Is64Bit_ReturnsFalseForX86()
    {
        var info = new PeInfo { Architecture = PeArchitecture.X86 };
        Assert.False(info.Is64Bit);
    }
    
    [Fact]
    public void Is64Bit_ReturnsFalseForUnknown()
    {
        var info = new PeInfo { Architecture = PeArchitecture.Unknown };
        Assert.False(info.Is64Bit);
    }
}

public class PackExceptionTests
{
    [Fact]
    public void Constructor_WithErrorCodeAndMessage()
    {
        var ex = new PackException(PackErrorCode.FileNotFound, "File not found");
        
        Assert.Equal(PackErrorCode.FileNotFound, ex.ErrorCode);
        Assert.Equal("File not found", ex.Message);
    }
    
    [Fact]
    public void Constructor_WithInnerException()
    {
        var inner = new System.IO.IOException("Disk error");
        var ex = new PackException(PackErrorCode.ReadFailed, "Read failed", inner);
        
        Assert.Equal(PackErrorCode.ReadFailed, ex.ErrorCode);
        Assert.Equal("Read failed", ex.Message);
        Assert.Same(inner, ex.InnerException);
    }
}

public class PackFileItemTests
{
    [Fact]
    public void DefaultValues_AreCorrect()
    {
        var item = new PackFileItem();
        
        Assert.Equal(string.Empty, item.SourcePath);
        Assert.Equal(string.Empty, item.VirtualPath);
        Assert.True(item.IsCompressed);
        Assert.True(item.IsVirtualized);
        Assert.Equal(0, item.OriginalSize);
    }
}
