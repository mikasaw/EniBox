using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using EniBox.Tests.TestInfrastructure;
using Xunit.Abstractions;

namespace EniBox.Tests.TestInfrastructure;

public abstract class E2ETestBase : VerificationTestBase, IDisposable
{
    protected TempFileHelper TempFiles { get; }
    protected LzmaCompressor Compressor { get; }
    protected VfsBuilder VfsBuilder { get; }
    protected EniBox.GUI.Services.PackService PackService { get; }
    
    protected E2ETestBase(ITestOutputHelper output) : base(output)
    {
        TempFiles = new TempFileHelper(GetType().Name);
        Compressor = new LzmaCompressor();
        VfsBuilder = new VfsBuilder(Compressor);
        PackService = new EniBox.GUI.Services.PackService(Compressor, VfsBuilder);
    }
    
    protected async Task<PackResult> PackExeAsync(
        string sourceExePath,
        string outputPath,
        bool enableRegistryVirtualization = false,
        bool enableSubProcessInjection = true)
    {
        var config = new PackConfiguration
        {
            SourceExePath = sourceExePath,
            OutputPath = outputPath,
            EnableRegistryVirtualization = enableRegistryVirtualization,
            EnableSubProcessInjection = enableSubProcessInjection
        };
        
        return await PackService.PackAsync(config, null, CancellationToken.None);
    }
    
    protected ProcessRunner RunPackedExe(string exePath, int timeoutMs = 15000)
    {
        return ProcessRunner.Run(exePath, "", timeoutMs);
    }
    
    protected static string GetSystemFcExePath()
    {
        var systemDir = Environment.SystemDirectory;
        var fcPath = Path.Combine(systemDir, "fc.exe");
        if (!File.Exists(fcPath))
            throw new FileNotFoundException($"fc.exe not found at {fcPath}");
        return fcPath;
    }
    
    protected static bool IsPeToolAvailable => PeToolAvailabilityChecker.IsAvailable;
    
    public new void Dispose()
    {
        TempFiles.Dispose();
        base.Dispose();
    }
}
