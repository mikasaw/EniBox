using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace EniBox.Tests.TestInfrastructure;

public sealed class TempFileHelper : IDisposable
{
    private readonly List<string> _tempPaths = new();
    private readonly string _testName;

    public TempFileHelper(string testName = "")
    {
        _testName = string.IsNullOrEmpty(testName) ? "unknown" : testName;
        CleanupStaleDirectories();
    }

    private string GetTempPrefix() =>
        $"EniBox-E2E-{_testName}-{Process.GetCurrentProcess().Id}-";

    public string CreateTempFile(string extension = ".tmp")
    {
        var path = Path.Combine(Path.GetTempPath(), $"{GetTempPrefix()}{Guid.NewGuid():N}{extension}");
        File.WriteAllText(path, "");
        _tempPaths.Add(path);
        return path;
    }

    public string CreateTempFile(byte[] content, string extension = ".tmp")
    {
        var path = Path.Combine(Path.GetTempPath(), $"{GetTempPrefix()}{Guid.NewGuid():N}{extension}");
        File.WriteAllBytes(path, content);
        _tempPaths.Add(path);
        return path;
    }

    public string CreateTempFile(string content, string extension = ".txt")
    {
        var path = Path.Combine(Path.GetTempPath(), $"{GetTempPrefix()}{Guid.NewGuid():N}{extension}");
        File.WriteAllText(path, content);
        _tempPaths.Add(path);
        return path;
    }

    public string CreateTempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"{GetTempPrefix()}{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        _tempPaths.Add(path);
        return path;
    }

    public static void CleanupStaleDirectories()
    {
        try
        {
            var tempRoot = Path.GetTempPath();
            var cutoff = DateTime.UtcNow.AddHours(-24);
            foreach (var dir in Directory.EnumerateDirectories(tempRoot, "EniBox-E2E-*"))
            {
                try
                {
                    if (Directory.GetCreationTimeUtc(dir) < cutoff)
                        Directory.Delete(dir, recursive: true);
                }
                catch
                {
                }
            }
        }
        catch
        {
        }
    }

    public void Dispose()
    {
        foreach (var path in _tempPaths)
        {
            try
            {
                if (File.Exists(path))
                    File.Delete(path);
                else if (Directory.Exists(path))
                    Directory.Delete(path, recursive: true);
            }
            catch
            {
                try
                {
                    if (Directory.Exists(path))
                    {
                        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
                        static extern bool MoveFileExW(string lpExistingFileName, string? lpNewFileName, uint dwFlags);

                        MoveFileExW(path, null, 0x00000004);
                    }
                }
                catch
                {
                }
            }
        }
        _tempPaths.Clear();
    }
}
