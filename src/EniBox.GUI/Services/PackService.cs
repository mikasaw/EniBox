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

                var sectionData = CombineSectionData(vfsResult, loaderData);

                var peError = ApplyPeModifications(config.SourceExePath, config.OutputPath, sectionData);
                if (peError != null)
                {
                    var err = peError.Value;
                    return new PackResult { IsSuccess = false, ErrorMessage = err.ErrorMessage!, ErrorCode = err.ErrorCode };
                }

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

        private static PeInfo? GetPeInfo(string exePath)
        {
            try
            {
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

        private static byte[] CombineSectionData(VfsBuildResult vfsResult, byte[] loaderData)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);

            var vfsMetadata = vfsResult.Metadata;
            var vfsDataRegion = vfsResult.DataRegion;
            uint vfsTotalSize = (uint)(vfsMetadata.Length + vfsDataRegion.Length);
            writer.Write(vfsTotalSize);

            writer.Write(vfsMetadata);
            writer.Write(vfsDataRegion);
            writer.Write(loaderData);

            return ms.ToArray();
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

                var importEntries = new PeToolInterop.ImportEntry[]
                {
                    new() { DllName = PackConstants.LoaderDllImportName }
                };
                result = PeToolInterop.MergeImports(ctx, importEntries);
                if (result != 0)
                    return (MessageConstants.FailedMergeImports + result + ").", PackErrorCode.ImportMergeFailed);

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
