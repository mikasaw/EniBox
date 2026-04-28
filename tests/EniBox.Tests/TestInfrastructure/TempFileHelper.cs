using System;
using System.Collections.Generic;
using System.IO;

namespace EniBox.Tests.TestInfrastructure;

public sealed class TempFileHelper : IDisposable
{
    private readonly List<string> _tempPaths = new();
    
    public string CreateTempFile(string extension = ".tmp")
    {
        var path = Path.Combine(Path.GetTempPath(), $"enibox_test_{Guid.NewGuid():N}{extension}");
        File.WriteAllText(path, "");
        _tempPaths.Add(path);
        return path;
    }
    
    public string CreateTempFile(byte[] content, string extension = ".tmp")
    {
        var path = Path.Combine(Path.GetTempPath(), $"enibox_test_{Guid.NewGuid():N}{extension}");
        File.WriteAllBytes(path, content);
        _tempPaths.Add(path);
        return path;
    }
    
    public string CreateTempFile(string content, string extension = ".txt")
    {
        var path = Path.Combine(Path.GetTempPath(), $"enibox_test_{Guid.NewGuid():N}{extension}");
        File.WriteAllText(path, content);
        _tempPaths.Add(path);
        return path;
    }
    
    public string CreateTempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), $"enibox_test_{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        _tempPaths.Add(path);
        return path;
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
            }
        }
        _tempPaths.Clear();
    }
}
