using System;
using Xunit.Abstractions;

namespace EniBox.Tests.TestInfrastructure;

public class TestLogger
{
    private readonly ITestOutputHelper _output;
    
    public TestLogger(ITestOutputHelper output)
    {
        _output = output ?? throw new ArgumentNullException(nameof(output));
    }
    
    public void Info(string message)
    {
        _output.WriteLine($"[INFO] {message}");
    }
    
    public void Warning(string message)
    {
        _output.WriteLine($"[WARN] {message}");
    }
    
    public void Error(string message)
    {
        _output.WriteLine($"[ERROR] {message}");
    }
    
    public void Debug(string message)
    {
        _output.WriteLine($"[DEBUG] {message}");
    }
    
    public void Success(string message)
    {
        _output.WriteLine($"[PASS] {message}");
    }
    
    public void Fail(string message)
    {
        _output.WriteLine($"[FAIL] {message}");
    }
}
