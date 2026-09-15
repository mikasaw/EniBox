#if CLI_MODE
using System;
using EniBox.GUI.Services;
using Microsoft.Extensions.DependencyInjection;

namespace EniBox.GUI
{
    /// <summary>
    /// Standalone CLI entry point for EniBox.
    /// Used when building with CLI_MODE=true to produce EniBox.CLI.exe
    /// without WPF/WinForms dependencies.
    /// </summary>
    public static class Program
    {
        public static int Main(string[] args)
        {
            try
            {
                if (args.Length == 0 || Array.Exists(args, a => a == "--help" || a == "-h"))
                {
                    Console.WriteLine("EniBox - Virtual File Box Packer (CLI)");
                    Console.WriteLine();
                    Console.WriteLine("Usage: EniBox.CLI --source <exe> --output <exe> [options]");
                    Console.WriteLine();
                    Console.WriteLine("Options:");
                    Console.WriteLine("  --source <path>              Path to the source EXE file (required)");
                    Console.WriteLine("  --output <path>              Path for the output file (required)");
                    Console.WriteLine("  --files <list>               Semicolon-separated list of dependency files");
                    Console.WriteLine("  --dirs <list>                Semicolon-separated list of dependency directories");
                    Console.WriteLine("  --compress <on|off>          Enable compression (default: on)");
                    Console.WriteLine("  --registry-virtualization    Enable registry virtualization (default: off)");
                    Console.WriteLine("  --subprocess-injection       Enable subprocess injection (default: on)");
                    Console.WriteLine("  --help, -h                   Show this help message");
                    return 0;
                }

                var services = new ServiceCollection();
                services.AddEniBoxServices();
                var cli = new CliRunner(services.BuildServiceProvider());
                return cli.Run(args);
            }
            catch (Exception ex)
            {
                // D8 顶层兜底：System.CommandLine 不会吞掉 handler 之外抛出的异常，
                // 不接住就会以丑陋的堆栈 + 模糊退出码崩出去。
                Services.ErrorLog.Write("CLI", ex);
                Console.Error.WriteLine(
                    $"Fatal: {ex.Message}{Environment.NewLine}详细信息已写入 {Services.ErrorLog.LogDirectory}");
                return 1;
            }
        }
    }
}
#endif
