using System;
using System.Collections.Concurrent;
using System.IO;

namespace EniBox.Tests.TestInfrastructure;

public static class TestExeBuilder
{
    private static readonly ConcurrentDictionary<string, string> _cache = new();
    private static readonly string HelpersRoot = Path.Combine(
        FileHelper.GetSolutionRoot(), "tests", "TestHelpers");
    
    public static string GetHelperPath(string name)
    {
        return _cache.GetOrAdd(name, BuildHelper);
    }
    
    private static string BuildHelper(string name)
    {
        var projectDir = Path.Combine(HelpersRoot, name);
        var publishDir = Path.Combine(projectDir, "publish");
        var exePath = Path.Combine(publishDir, $"{name}.exe");
        
        if (File.Exists(exePath))
            return exePath;
        
        throw new FileNotFoundException(
            $"Test helper '{name}' not built. Run: dotnet publish {projectDir} -c Release -r win-x64 --self-contained false -o {publishDir}");
    }
    
    public static bool IsHelperAvailable(string name)
    {
        try
        {
            GetHelperPath(name);
            return true;
        }
        catch
        {
            return false;
        }
    }
}
