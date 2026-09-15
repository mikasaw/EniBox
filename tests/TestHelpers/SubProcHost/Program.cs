using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

class Program
{
    static int Main(string[] args)
    {
        if (args.Length < 1)
        {
            Console.WriteLine("Usage: SubProcHost <child_exe_path>");
            return 4;
        }

        var childPath = args[0];
        var startInfo = new ProcessStartInfo
        {
            FileName = childPath,
            // 透传剩余参数给子进程（例如 SubProcChild 要读的 VFS 路径）
            Arguments = args.Length >= 2 ? args[1] : string.Empty,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        try
        {
            using var process = Process.Start(startInfo);
            if (process == null)
            {
                Console.WriteLine("CHECK:SUBPROC_CREATE:FAIL:Process.Start returned null");
                return 4;
            }

            Console.WriteLine($"CHECK:SUBPROC_CREATE:OK:pid={process.Id}");

            var output = process.StandardOutput.ReadToEnd();
            process.WaitForExit(10000);

            Console.Write(output);
            Console.WriteLine($"CHECK:SUBPROC_EXIT:OK:code={process.ExitCode}");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"CHECK:SUBPROC_CREATE:FAIL:{ex.Message}");
            return 4;
        }
    }
}
