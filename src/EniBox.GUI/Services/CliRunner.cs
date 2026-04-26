using System;
using System.Collections.Generic;
using System.CommandLine;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public sealed class CliRunner
    {
        public int Run(string[] args)
        {
            var sourceOption = new Option<string>("--source", "Path to the source EXE file") { IsRequired = true };
            var outputOption = new Option<string>("--output", "Path for the output file") { IsRequired = true };
            var filesOption = new Option<string>("--files", "Semicolon-separated list of dependency files");
            var dirsOption = new Option<string>("--dirs", "Semicolon-separated list of dependency directories");
            var compressOption = new Option<bool>("--compress", () => true, "Enable compression (on/off)");
            var regVirtOption = new Option<bool>("--registry-virtualization", () => false, "Enable registry virtualization");
            var subProcOption = new Option<bool>("--subprocess-injection", () => true, "Enable subprocess injection");

            var rootCommand = new RootCommand("EniBox - Virtual File Box Packer")
            {
                sourceOption, outputOption, filesOption, dirsOption,
                compressOption, regVirtOption, subProcOption
            };

            rootCommand.SetHandler(async (context) =>
            {
                var source = context.ParseResult.GetValueForOption(sourceOption)!;
                var output = context.ParseResult.GetValueForOption(outputOption)!;
                var files = context.ParseResult.GetValueForOption(filesOption) ?? "";
                var dirs = context.ParseResult.GetValueForOption(dirsOption) ?? "";
                var compress = context.ParseResult.GetValueForOption(compressOption);
                var regVirt = context.ParseResult.GetValueForOption(regVirtOption);
                var subProc = context.ParseResult.GetValueForOption(subProcOption);

                var config = new PackConfiguration
                {
                    SourceExePath = source,
                    OutputPath = output,
                    EnableRegistryVirtualization = regVirt,
                    EnableSubProcessInjection = subProc
                };

                // Add files
                var baseDir = Path.GetDirectoryName(source) ?? "";
                foreach (var file in files.Split(';', StringSplitOptions.RemoveEmptyEntries))
                {
                    if (File.Exists(file))
                    {
                        var item = PackFileItem.FromFile(file, baseDir);
                        item.IsCompressed = compress;
                        config.Files.Add(item);
                    }
                }

                // Add directories
                foreach (var dir in dirs.Split(';', StringSplitOptions.RemoveEmptyEntries))
                {
                    if (Directory.Exists(dir))
                    {
                        foreach (var file in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
                        {
                            var item = PackFileItem.FromFile(file, baseDir);
                            item.IsCompressed = compress;
                            config.Files.Add(item);
                        }
                    }
                }

                var compressor = new LzmaCompressor();
                var vfsBuilder = new VfsBuilder(compressor);
                var packService = new PackService(compressor, vfsBuilder);

                var progress = new Progress<PackProgress>(p =>
                {
                    Console.Write($"\r[{p.Stage}] {p.ProgressPercent:F1}% - {p.CurrentFile}");
                });

                using var cts = new CancellationTokenSource();
                Console.CancelKeyPress += (s, e) =>
                {
                    e.Cancel = true;
                    cts.Cancel();
                    Console.WriteLine("\nCancelling...");
                };

                try
                {
                    var result = await packService.PackAsync(config, progress, cts.Token);

                    Console.WriteLine();
                    if (result.IsSuccess)
                    {
                        Console.WriteLine($"Success: {result.OutputPath} ({result.OutputFileSize} bytes)");
                        context.ExitCode = 0;
                    }
                    else
                    {
                        Console.Error.WriteLine($"Error: {result.ErrorMessage}");
                        context.ExitCode = 1;
                    }
                }
                catch (OperationCanceledException)
                {
                    Console.WriteLine("\nCancelled.");
                    context.ExitCode = 2;
                }
            });

            return rootCommand.Invoke(args);
        }
    }
}
