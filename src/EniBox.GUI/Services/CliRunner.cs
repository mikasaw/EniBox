using System;
using System.Collections.Generic;
using System.CommandLine;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using Microsoft.Extensions.DependencyInjection;

namespace EniBox.GUI.Services
{
    public sealed class CliRunner
    {
        private readonly IServiceProvider _serviceProvider;

        public CliRunner(IServiceProvider serviceProvider)
        {
            _serviceProvider = serviceProvider;
        }

        public int Run(string[] args)
        {
            var sourceOption = new Option<string>("--source", "Path to the source EXE file") { IsRequired = true };
            var outputOption = new Option<string>("--output", "Path for the output file") { IsRequired = true };
            var filesOption = new Option<string>("--files", "Semicolon-separated list of dependency files");
            var dirsOption = new Option<string>("--dirs", "Semicolon-separated list of dependency directories");
            // System.CommandLine beta4 的 Option<bool> 只认 true/false——帮助文档
            // 承诺的 on|off 会在解析阶段直接报错退出。用字符串接住再自行归一。
            var compressOption = new Option<string>("--compress", () => "on", "Enable compression: on|off (default on)");
            var regVirtOption = new Option<bool>("--registry-virtualization", () => false, "Enable registry virtualization");
            var subProcOption = new Option<bool>("--subprocess-injection", () => true, "Enable subprocess injection");
            // 预置注册表值，可重复：KEY|NAME|TYPE|DATA 或 KEY|NAME|DATA（TYPE 缺省 SZ）
            // TYPE ∈ {SZ, EXPAND_SZ, DWORD, BINARY}，详见 PackRegistryValue.FromSpec
            var regValueOption = new Option<string[]>("--registry-value",
                "Preset registry value, repeatable: KEY|NAME|TYPE|DATA or KEY|NAME|DATA (SZ default); TYPE: SZ|EXPAND_SZ|DWORD|BINARY")
            {
                AllowMultipleArgumentsPerToken = false
            };

            var rootCommand = new RootCommand("EniBox - Virtual File Box Packer")
            {
                sourceOption, outputOption, filesOption, dirsOption,
                compressOption, regVirtOption, subProcOption, regValueOption
            };

            rootCommand.SetHandler(async (context) =>
            {
                var source = context.ParseResult.GetValueForOption(sourceOption)!;
                var output = context.ParseResult.GetValueForOption(outputOption)!;
                var files = context.ParseResult.GetValueForOption(filesOption) ?? "";
                var dirs = context.ParseResult.GetValueForOption(dirsOption) ?? "";
                var compressRaw = context.ParseResult.GetValueForOption(compressOption) ?? "on";
                var regVirt = context.ParseResult.GetValueForOption(regVirtOption);
                var subProc = context.ParseResult.GetValueForOption(subProcOption);
                var regValueSpecs = context.ParseResult.GetValueForOption(regValueOption) ?? Array.Empty<string>();

                if (!TryParseOnOff(compressRaw, out var compress))
                {
                    Console.Error.WriteLine($"Error: --compress 的值无效: '{compressRaw}'（支持 on|off|true|false|yes|no|1|0）");
                    context.ExitCode = 1;
                    return;
                }

                var config = new PackConfiguration
                {
                    SourceExePath = source,
                    OutputPath = output,
                    EnableRegistryVirtualization = regVirt,
                    EnableSubProcessInjection = subProc
                };

                // 预置注册表值：解析失败属使用错误，报错退出而非静默丢值
                foreach (var spec in regValueSpecs)
                {
                    try
                    {
                        config.RegistryValues.Add(PackRegistryValue.FromSpec(spec));
                    }
                    catch (ArgumentException ex)
                    {
                        Console.Error.WriteLine($"Error: --registry-value '{spec}': {ex.Message}");
                        context.ExitCode = 1;
                        return;
                    }
                }

                if (regVirt && config.RegistryValues.Count == 0)
                {
                    Console.Error.WriteLine("Warning: --registry-virtualization 已启用但无 --registry-value 预置值：无作用域声明，虚拟化为空操作");
                }
                if (!regVirt && config.RegistryValues.Count > 0)
                {
                    Console.Error.WriteLine("Warning: 已提供 --registry-value 但未启用 --registry-virtualization：预置值不会写入产物（PackService 仅在开关开启时消费）");
                }

                // Add files — 不存在的路径是使用错误，必须报错退出而非静默跳过
                //（静默跳过会产出一个"内容比预期少"的包且退出码为 0）
                var baseDir = Path.GetDirectoryName(source) ?? "";
                var missingPaths = new List<string>();
                foreach (var file in files.Split(';', StringSplitOptions.RemoveEmptyEntries))
                {
                    if (File.Exists(file))
                    {
                        var item = PackFileItem.FromFile(file, baseDir);
                        item.IsCompressed = compress;
                        config.Files.Add(item);
                    }
                    else
                    {
                        missingPaths.Add(file);
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
                    else
                    {
                        missingPaths.Add(dir);
                    }
                }

                if (missingPaths.Count > 0)
                {
                    Console.Error.WriteLine($"Error: --files/--dirs 指定的路径不存在: {string.Join("; ", missingPaths)}");
                    context.ExitCode = 1;
                    return;
                }

                var packService = (IPackService)_serviceProvider.GetService(typeof(IPackService))!;

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

        private static bool TryParseOnOff(string raw, out bool value)
        {
            switch (raw.Trim().ToLowerInvariant())
            {
                case "on":
                case "true":
                case "yes":
                case "1":
                    value = true;
                    return true;
                case "off":
                case "false":
                case "no":
                case "0":
                    value = false;
                    return true;
                default:
                    value = false;
                    return false;
            }
        }
    }
}
