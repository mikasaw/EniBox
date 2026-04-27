using System;
using System.Collections.Generic;
using System.IO;

namespace EniBox.Tests.TestInfrastructure;

public class TestContext
{
    public string TestName { get; set; } = string.Empty;
    public string TestCategory { get; set; } = string.Empty;
    public DateTime StartTime { get; set; }
    public DateTime EndTime { get; set; }
    public bool IsPassed { get; set; }
    public string? ErrorMessage { get; set; }
    public List<string> VerificationSteps { get; } = new();
    public Dictionary<string, object> Metrics { get; } = new();
    
    public void AddStep(string stepName, bool passed, string? detail = null)
    {
        var status = passed ? "✓" : "✗";
        var message = $"{status} {stepName}";
        if (!string.IsNullOrEmpty(detail))
        {
            message += $" - {detail}";
        }
        VerificationSteps.Add(message);
    }
    
    public void AddMetric(string name, object value)
    {
        Metrics[name] = value;
    }
    
    public TimeSpan Duration => EndTime - StartTime;
}
