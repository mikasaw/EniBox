using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace EniBox.Tests.TestInfrastructure;

public static class ProcessHelper
{
    public static bool Is64BitProcess(int processId)
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            throw new PlatformNotSupportedException("仅支持Windows平台");
        }
        
        var process = Process.GetProcessById(processId);
        return Is64BitProcess(process);
    }
    
    public static bool Is64BitProcess(Process process)
    {
        if (!Environment.Is64BitOperatingSystem)
        {
            return false;
        }
        
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            return IsWow64Process(process.Handle, out bool isWow64) && !isWow64;
        }
        
        return false;
    }
    
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool IsWow64Process(IntPtr hProcess, out bool wow64Process);
    
    public static Process? FindProcessByName(string processName)
    {
        var processes = Process.GetProcessesByName(processName);
        return processes.Length > 0 ? processes[0] : null;
    }
    
    public static bool IsProcessRunning(string processName)
    {
        return Process.GetProcessesByName(processName).Length > 0;
    }
    
    public static void KillProcessSafe(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill();
                process.WaitForExit(5000);
            }
        }
        catch
        {
        }
    }
}
