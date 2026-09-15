using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace EniBox.Tests.TestInfrastructure;

public sealed class ProcessRunner
{
    public int ExitCode { get; private set; }
    public string StandardOutput { get; private set; } = "";
    public string StandardError { get; private set; } = "";
    public bool TimedOut { get; private set; }
    
    public static ProcessRunner Run(string exePath, string arguments = "", int timeoutMs = 30000)
    {
        var result = new ProcessRunner();

        // CreateProcessW (UseShellExecute=false) accepts any valid PE regardless
        // of file extension — .enibox outputs launch fine without a shell layer.
        // (Routing through "cmd.exe /c \"exe\" args" is NOT an option: cmd's /c
        // quote-stripping mangles quoted arguments, leaving literal quote
        // characters inside paths -> ERROR_INVALID_NAME at CreateFile.)
        var fileName = exePath;
        var fileArgs = arguments;

        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = fileName,
                Arguments = fileArgs,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            }
        };

        process.Start();
        
        var outputLines = new List<string>();
        var errorLines = new List<string>();
        
        process.OutputDataReceived += (s, e) => { if (e.Data != null) outputLines.Add(e.Data); };
        process.ErrorDataReceived += (s, e) => { if (e.Data != null) errorLines.Add(e.Data); };
        
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        
        if (!process.WaitForExit(timeoutMs))
        {
            result.TimedOut = true;
            try { process.Kill(); } catch { }
            process.WaitForExit(5000);
        }
        else
        {
            // 正常退出时也要排空异步输出：WaitForExit(int) 重载不等
            // OutputDataReceived 异步泵排空，CI 慢 VM 上会拿到空 stdout
            // （.NET 文档明确要求超时重载后再调无参 WaitForExit）。
            process.WaitForExit();
        }
        
        result.ExitCode = process.ExitCode;
        result.StandardOutput = string.Join(Environment.NewLine, outputLines);
        result.StandardError = string.Join(Environment.NewLine, errorLines);
        
        return result;
    }
    
    public IEnumerable<CheckResult> ParseCheckResults()
    {
        foreach (var line in StandardOutput.Split('\n'))
        {
            var trimmed = line.Trim();
            if (trimmed.StartsWith("CHECK:"))
            {
                var parts = trimmed.Substring(6).Split(':');
                if (parts.Length >= 2)
                {
                    yield return new CheckResult
                    {
                        Category = parts[0],
                        Passed = parts[1] == "OK",
                        Detail = parts.Length >= 3 ? parts[2] : ""
                    };
                }
            }
        }
    }
}

public sealed class CheckResult
{
    public string Category { get; set; } = "";
    public bool Passed { get; set; }
    public string Detail { get; set; } = "";
}
