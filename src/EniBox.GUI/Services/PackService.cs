using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using EniBox.GUI.Interop;
using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public sealed class PackService : IPackService
    {
        private readonly ICompressor _compressor;
        private readonly IVfsBuilder _vfsBuilder;

        public PackService(ICompressor compressor, IVfsBuilder vfsBuilder)
        {
            _compressor = compressor;
            _vfsBuilder = vfsBuilder;
        }

        public PackResult Pack(
            PackConfiguration config,
            IProgress<PackProgress>? progressCallback,
            CancellationToken cancellationToken)
        {
            string? tempOutputPath = null;
            try
            {
                ReportProgress(progressCallback, PackStage.CollectingFiles, 0, config.Files.Count, "Validating input...");

                var validationError = ValidateInput(config);
                if (validationError != null)
                    return new PackResult { IsSuccess = false, ErrorMessage = validationError, ErrorCode = PackErrorCode.ValidationFailed };

                cancellationToken.ThrowIfCancellationRequested();

                ReportProgress(progressCallback, PackStage.CollectingFiles, 0, config.Files.Count, "Parsing source EXE...");

                var peInfo = GetPeInfo(config.SourceExePath);
                if (peInfo == null)
                    return new PackResult { IsSuccess = false, ErrorMessage = MessageConstants.FailedParsePe, ErrorCode = PackErrorCode.InvalidPe };

                cancellationToken.ThrowIfCancellationRequested();

                ReportProgress(progressCallback, PackStage.BuildingVFS, 0, config.Files.Count, "Building VFS...");

                _vfsBuilder.Clear();
                foreach (var file in config.Files)
                {
                    _vfsBuilder.AddFile(file);
                }

                var vfsResult = _vfsBuilder.Build();

                cancellationToken.ThrowIfCancellationRequested();

                ReportProgress(progressCallback, PackStage.Compressing, 0, config.Files.Count, "Compression complete.");

                ReportProgress(progressCallback, PackStage.ModifyingPE, 0, config.Files.Count, "Modifying PE structure...");

                var loaderData = SelectLoaderDll(peInfo.Architecture);
                if (loaderData == null)
                    return new PackResult { IsSuccess = false, ErrorMessage = MessageConstants.UnsupportedArch + peInfo.Architecture + MessageConstants.ArchSupportSuffix, ErrorCode = PackErrorCode.UnsupportedArch };

                if (peInfo.Architecture != PeArchitecture.X64)
                    return new PackResult { IsSuccess = false, ErrorMessage = MessageConstants.UnsupportedArch + peInfo.Architecture + MessageConstants.ArchSupportSuffix, ErrorCode = PackErrorCode.UnsupportedArch };

                var sectionData = CombineSectionData(vfsResult, loaderData);

                var peError = ApplyPeModifications(config.SourceExePath, config.OutputPath, sectionData);
                if (peError != null)
                {
                    var err = peError.Value;
                    return new PackResult { IsSuccess = false, ErrorMessage = err.ErrorMessage!, ErrorCode = err.ErrorCode };
                }

                // The packed image statically imports EniBox.Loader.dll — the DLL
                // must exist next to the output or Windows refuses to start the
                // process at all. Write the exact bytes that were embedded.
                var sidecarError = WriteLoaderSidecar(config.OutputPath, loaderData);
                if (sidecarError != null)
                    return new PackResult { IsSuccess = false, ErrorMessage = sidecarError, ErrorCode = PackErrorCode.WriteFailed };

                cancellationToken.ThrowIfCancellationRequested();

                ReportProgress(progressCallback, PackStage.WritingOutput, config.Files.Count, config.Files.Count, "Writing output...");

                var outputInfo = new FileInfo(config.OutputPath);
                ReportProgress(progressCallback, PackStage.Completed, config.Files.Count, config.Files.Count, "Packaging complete.");

                return new PackResult
                {
                    IsSuccess = true,
                    OutputPath = config.OutputPath,
                    OutputFileSize = outputInfo.Exists ? outputInfo.Length : 0
                };
            }
            catch (OperationCanceledException)
            {
                CleanupTempFile(tempOutputPath);
                return new PackResult { IsSuccess = false, ErrorMessage = MessageConstants.OperationCancelled, ErrorCode = PackErrorCode.OperationCancelled };
            }
            catch (PackException ex)
            {
                CleanupTempFile(tempOutputPath);
                return new PackResult { IsSuccess = false, ErrorMessage = string.Format(MessageConstants.PackErrorFormat, ex.ErrorCode, ex.Message) };
            }
            catch (Exception ex)
            {
                CleanupTempFile(tempOutputPath);
                return new PackResult { IsSuccess = false, ErrorMessage = MessageConstants.UnexpectedError + ex.Message, ErrorCode = PackErrorCode.UnexpectedError };
            }
        }

        public Task<PackResult> PackAsync(
            PackConfiguration config,
            IProgress<PackProgress>? progressCallback,
            CancellationToken cancellationToken)
        {
            return Task.FromResult(Pack(config, progressCallback, cancellationToken));
        }

        private static string? ValidateInput(PackConfiguration config)
        {
            if (string.IsNullOrWhiteSpace(config.SourceExePath))
                return MessageConstants.SourceExeNotSpecified;

            if (!File.Exists(config.SourceExePath))
                return MessageConstants.SourceExeNotFound + config.SourceExePath;

            if (string.IsNullOrWhiteSpace(config.OutputPath))
                return MessageConstants.OutputNotSpecified;

            try
            {
                var outputDir = Path.GetDirectoryName(config.OutputPath);
                if (!string.IsNullOrEmpty(outputDir) && !Directory.Exists(outputDir))
                    Directory.CreateDirectory(outputDir);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Output path validation failed: {ex.Message}");
                return MessageConstants.OutputNotWritable + config.OutputPath;
            }

            foreach (var file in config.Files)
            {
                if (!File.Exists(file.SourcePath))
                    return MessageConstants.DependencyNotFound + file.SourcePath;
            }

            return null;
        }

        /// <summary>
        /// Quick pre-validation of PE header structure before calling native code.
        /// Prevents native DLL crashes on severely malformed PEs (e.g. NumberOfSections=0).
        /// </summary>
        private static bool TryValidatePeStructure(string exePath)
        {
            try
            {
                byte[] header = new byte[1024];
                using var fs = new FileStream(exePath, FileMode.Open, FileAccess.Read, FileShare.Read);
                int read = fs.Read(header, 0, header.Length);
                if (read < 64) return false;

                // Check DOS magic
                if (header[0] != 0x4D || header[1] != 0x5A) return false;

                // Read e_lfanew at offset 0x3C
                int peOffset = header[0x3C] | (header[0x3D] << 8) | (header[0x3E] << 16) | (header[0x3F] << 24);
                if (peOffset < 64 || peOffset + 24 > read) return false;

                // Check PE signature
                if (header[peOffset] != 0x50 || header[peOffset + 1] != 0x45 ||
                    header[peOffset + 2] != 0x00 || header[peOffset + 3] != 0x00) return false;

                // Read NumberOfSections (offset +6 from PE signature)
                int numSections = header[peOffset + 6] | (header[peOffset + 7] << 8);
                if (numSections <= 0) return false;

                // Read SizeOfOptionalHeader (offset +20 from PE signature)
                int sizeOfOptionalHeader = header[peOffset + 20] | (header[peOffset + 21] << 8);
                if (sizeOfOptionalHeader <= 0) return false;

                return true;
            }
            catch
            {
                return false;
            }
        }

        private static PeInfo? GetPeInfo(string exePath)
        {
            try
            {
                // Pre-validate PE header structure before calling native code (defense-in-depth)
                if (!TryValidatePeStructure(exePath))
                    return null;

                int result = PeToolInterop.Open(exePath, out IntPtr ctx);
                if (result != 0 || ctx == IntPtr.Zero)
                    return null;
                try
                {
                    return PeToolInterop.GetInfo(ctx);
                }
                finally
                {
                    PeToolInterop.Close(ctx);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"PeInfo parsing failed for '{exePath}': {ex.Message}");
                return null;
            }
        }

        private static byte[]? SelectLoaderDll(PeArchitecture architecture)
        {
            string resourceName = architecture switch
            {
                PeArchitecture.X86 => PackConstants.LoaderDllX86Resource,
                PeArchitecture.X64 => PackConstants.LoaderDllX64Resource,
                _ => null!
            };

            if (resourceName == null)
                return null;

            var assembly = Assembly.GetExecutingAssembly();
            using var stream = assembly.GetManifestResourceStream(resourceName);
            if (stream == null)
                return null;

            var data = new byte[stream.Length];
            stream.ReadExactly(data, 0, data.Length);
            return data;
        }

    /// <summary>
    /// .enibox section layout (consumed by the packed image's bootstrap and
    /// the Loader's DllMain; x64 only):
    ///   [0..207]      bootstrap machine code (loads EniBox.Loader.dll via
    ///                 LoadLibraryA resolved through the PEB, then jumps back
    ///                 to the original entry point)
    ///   [208..213]    jmp [rip+0] stub (written by PeTool)
    ///   [214..221]    VA placeholder (written by PeTool as original ep RVA,
    ///                 patched by the Loader's DllMain to ImageBase + ep)
    ///   [222..279]    reserved zeros
    ///   [280..283]    original ep rva   (written by PeTool)
    ///   [284..287]    .enibox section rva (written by PeTool)
    ///   [288..291]    vfs_total_size
    ///   [292..295]    loader_total_size
    ///   [296..]       VFS metadata + VFS data + Loader DLL bytes
    ///   [tail]        reserved (unused; kept for layout stability)
    /// </summary>
    private static readonly byte[] BootstrapCode =
    {
        0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x40,
        0x18, 0x4C, 0x8B, 0x68, 0x20, 0x49, 0x8B, 0x4D, 0x00, 0x48, 0x8B, 0x09, 0x4C, 0x8B, 0x61, 0x20,
        0x41, 0x8B, 0x44, 0x24, 0x3C, 0x48, 0x63, 0xC0, 0x45, 0x8B, 0x8C, 0x04, 0x88, 0x00, 0x00, 0x00,
        0x4F, 0x8D, 0x0C, 0x0C, 0x45, 0x8B, 0x51, 0x18, 0x45, 0x8B, 0x59, 0x20, 0x4F, 0x8D, 0x1C, 0x1C,
        0x4C, 0x8D, 0x35, 0x69, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x3D, 0x6F, 0x00, 0x00, 0x00, 0x33, 0xDB,
        0x41, 0x3B, 0xDA, 0x73, 0x38, 0x41, 0x8B, 0x04, 0x9B, 0x49, 0x8D, 0x3C, 0x04, 0x49, 0x8B, 0xF6,
        0x48, 0x33, 0xC9, 0x8A, 0x04, 0x0F, 0x3A, 0x04, 0x0E, 0x75, 0x09, 0x48, 0xFF, 0xC1, 0x84, 0xC0,
        0x75, 0xF1, 0xEB, 0x04, 0xFF, 0xC3, 0xEB, 0xD8, 0x45, 0x8B, 0x41, 0x1C, 0x4F, 0x8D, 0x04, 0x04,
        0x41, 0x8B, 0x04, 0x98, 0x49, 0x8D, 0x04, 0x04, 0x49, 0x8B, 0xCF, 0xFF, 0xD0, 0x48, 0x83, 0xC4,
        0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x40, 0x18, 0x48, 0x8B,
        0x40, 0x20, 0x48, 0x8B, 0x40, 0x20, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x48, 0x03, 0xC2, 0xFF, 0xE0,
        0x4C, 0x6F, 0x61, 0x64, 0x4C, 0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x41, 0x00, 0x45, 0x6E, 0x69,
        0x42, 0x6F, 0x78, 0x2E, 0x4C, 0x6F, 0x61, 0x64, 0x65, 0x72, 0x2E, 0x64, 0x6C, 0x6C, 0x00, 0x00
    };

    private const int BootstrapRegionSize = 288; // VFS metadata starts here

    private static byte[] CombineSectionData(VfsBuildResult vfsResult, byte[] loaderData)
    {
        using var ms = new MemoryStream();
        using var writer = new BinaryWriter(ms);

        // [0..295] bootstrap region: code at [0..207], the jmp stub, the VA
        // placeholder and ep/section RVAs are filled in by PeTool afterwards.
        var bootstrap = new byte[BootstrapRegionSize];
        Array.Copy(BootstrapCode, bootstrap, BootstrapCode.Length);
        writer.Write(bootstrap);

        var vfsMetadata = vfsResult.Metadata;
        var vfsDataRegion = vfsResult.DataRegion;
        uint vfsTotalSize = (uint)(vfsMetadata.Length + vfsDataRegion.Length);
        writer.Write(vfsTotalSize);
        writer.Write((uint)loaderData.Length);

        writer.Write(vfsMetadata);
        writer.Write(vfsDataRegion);
        writer.Write(loaderData);

        return ms.ToArray();
    }

        private static string? WriteLoaderSidecar(string outputPath, byte[] loaderData)
        {
            try
            {
                var outputDir = Path.GetDirectoryName(outputPath);
                if (string.IsNullOrEmpty(outputDir))
                    outputDir = ".";
                File.WriteAllBytes(Path.Combine(outputDir, PackConstants.LoaderDllImportName), loaderData);
                return null;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Failed to write loader sidecar next to '{outputPath}': {ex.Message}");
                return MessageConstants.UnexpectedError + ex.Message;
            }
        }

        private static (string? ErrorMessage, int ErrorCode)? ApplyPeModifications(string sourcePath, string outputPath, byte[] sectionData)
        {
            IntPtr ctx = IntPtr.Zero;
            try
            {
                int result = PeToolInterop.Open(sourcePath, out ctx);
                if (result != 0 || ctx == IntPtr.Zero)
                    return (MessageConstants.FailedOpenPe + result + ").", PackErrorCode.InvalidPe);

                result = PeToolInterop.AddSection(ctx, PackConstants.EniboxSectionName, sectionData, PackConstants.EniboxSectionCharacteristics);
                if (result != 0)
                    return (MessageConstants.FailedAddSection + result + ").", PackErrorCode.SectionFull);

                result = PeToolInterop.ProcessTLS(ctx);
                if (result != 0)
                    return (MessageConstants.FailedProcessTls + result + ").", PackErrorCode.ImportMergeFailed);

                /* NOTE: the import table is intentionally left untouched. On
                 * Win11 25H2+ the loader hardening ignores appended import
                 * tables; the Loader DLL is instead loaded by the bootstrap
                 * at the entry point (see CombineSectionData). */

                result = PeToolInterop.Save(ctx, outputPath);
                if (result != 0)
                    return (MessageConstants.FailedSavePe + result + ").", PackErrorCode.WriteFailed);

                return null;
            }
            catch (DllNotFoundException ex)
            {
                return (MessageConstants.PeToolNotFoundPrefix + ex.Message + MessageConstants.PeToolNotFoundSuffix, PackErrorCode.LoaderNotFound);
            }
            catch (Exception ex)
            {
                return (MessageConstants.PeModificationFailed + ex.Message, PackErrorCode.UnexpectedError);
            }
            finally
            {
                if (ctx != IntPtr.Zero)
                    PeToolInterop.Close(ctx);
            }
        }

        private static void ReportProgress(IProgress<PackProgress>? progress, PackStage stage,
            int processed, int total, string currentFile)
        {
            progress?.Report(new PackProgress
            {
                Stage = stage,
                ProcessedFiles = processed,
                TotalFiles = total,
                CurrentFile = currentFile,
                ProgressPercent = total > 0 ? (double)processed / total * 100 : 0
            });
        }

        private static void CleanupTempFile(string? path)
        {
            if (path != null && File.Exists(path))
            {
                try { File.Delete(path); }
                catch (Exception ex) { Debug.WriteLine($"Failed to cleanup temp file '{path}': {ex.Message}"); }
            }
        }
    }
}
