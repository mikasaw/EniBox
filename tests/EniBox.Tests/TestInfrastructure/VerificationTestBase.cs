using System;
using System.IO;
using Xunit;
using Xunit.Abstractions;

namespace EniBox.Tests.TestInfrastructure;

public abstract class VerificationTestBase : IDisposable
{
    protected ITestOutputHelper Output { get; }
    protected string TestTempDir { get; }
    protected TestLogger Logger { get; }
    
    protected VerificationTestBase(ITestOutputHelper output)
    {
        Output = output ?? throw new ArgumentNullException(nameof(output));
        Logger = new TestLogger(output);
        
        TestTempDir = Path.Combine(
            Path.GetTempPath(),
            "EniBox_VerificationTests",
            Guid.NewGuid().ToString("N")
        );
        
        Directory.CreateDirectory(TestTempDir);
        Logger.Info($"测试临时目录: {TestTempDir}");
    }
    
    protected string CreateTempFile(string fileName, byte[] content)
    {
        var filePath = Path.Combine(TestTempDir, fileName);
        File.WriteAllBytes(filePath, content);
        return filePath;
    }
    
    protected string CreateTempFile(string fileName, string content)
    {
        var filePath = Path.Combine(TestTempDir, fileName);
        File.WriteAllText(filePath, content);
        return filePath;
    }
    
    protected string GetTestResourcePath(string resourceName)
    {
        var baseDir = AppDomain.CurrentDomain.BaseDirectory;
        var resourcePath = Path.Combine(baseDir, "TestResources", resourceName);
        
        if (!File.Exists(resourcePath))
        {
            throw new FileNotFoundException($"测试资源文件未找到: {resourcePath}");
        }
        
        return resourcePath;
    }
    
    public virtual void Dispose()
    {
        try
        {
            if (Directory.Exists(TestTempDir))
            {
                Directory.Delete(TestTempDir, recursive: true);
                Logger.Info($"已清理测试临时目录: {TestTempDir}");
            }
        }
        catch (Exception ex)
        {
            Logger.Warning($"清理测试临时目录失败: {ex.Message}");
        }
    }
}
