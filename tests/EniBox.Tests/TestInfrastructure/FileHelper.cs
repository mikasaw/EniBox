using System;
using System.IO;

namespace EniBox.Tests.TestInfrastructure;

public static class FileHelper
{
    public static string GetSolutionRoot()
    {
        var currentDir = AppDomain.CurrentDomain.BaseDirectory;
        var dir = new DirectoryInfo(currentDir);
        
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "EniBox.sln")))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        
        throw new DirectoryNotFoundException("未找到解决方案根目录");
    }
    
    public static string GetProjectDir(string projectName)
    {
        var solutionRoot = GetSolutionRoot();
        var projectDir = Path.Combine(solutionRoot, "src", projectName);
        
        if (!Directory.Exists(projectDir))
        {
            throw new DirectoryNotFoundException($"项目目录未找到: {projectDir}");
        }
        
        return projectDir;
    }
    
    public static string GetLoaderDllPath(bool is64Bit)
    {
        var solutionRoot = GetSolutionRoot();
        var archDir = is64Bit ? "x64" : "x86";
        var dllName = is64Bit ? "EniBox.Loader.x64.dll" : "EniBox.Loader.x86.dll";
        
        var dllPath = Path.Combine(solutionRoot, "src", "EniBox.Loader", archDir, "Release", dllName);
        
        if (!File.Exists(dllPath))
        {
            throw new FileNotFoundException($"Loader DLL未找到: {dllPath}");
        }
        
        return dllPath;
    }
    
    public static string GetPeToolDllPath(bool is64Bit)
    {
        var solutionRoot = GetSolutionRoot();
        var archDir = is64Bit ? "x64" : "x86";
        var dllName = "EniBox.PeTool.dll";
        
        var dllPath = Path.Combine(solutionRoot, "src", "EniBox.PeTool", archDir, "Release", dllName);
        
        if (!File.Exists(dllPath))
        {
            throw new FileNotFoundException($"PeTool DLL未找到: {dllPath}");
        }
        
        return dllPath;
    }
}
