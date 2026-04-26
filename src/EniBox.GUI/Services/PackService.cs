using System;
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

        public async Task<PackResult> PackAsync(
            PackConfiguration config,
            IProgress<PackProgress>? progressCallback,
            CancellationToken cancellationToken)
        {
            string? tempOutputPath = null;
            try
            {
                await Task.CompletedTask; // Ensure async behavior for cancellation support

                // Stage 1: Validate input
                ReportProgress(progressCallback, PackStage.CollectingFiles, 0, config.Files.Count, "Validating input...");

                var validationError = ValidateInput(config);
                if (validationError != null)
                    return new PackResult { IsSuccess = false, ErrorMessage = validationError, ErrorCode = PackErrorCode.ValidationFailed };

                cancellationToken.ThrowIfCancellationRequested();

                // Stage 2: Parse source PE
                ReportProgress(progressCallback, PackStage.CollectingFiles, 0, config.Files.Count, "Parsing source EXE...");

                var peInfo = ParsePeInfo(config.SourceExePath);
                if (peInfo == null)
                    return new PackResult { IsSuccess = false, ErrorMessage = "Failed to parse source EXE PE structure.", ErrorCode = PackErrorCode.InvalidPe };

                cancellationToken.ThrowIfCancellationRequested();

                // Stage 3: Build VFS
                ReportProgress(progressCallback, PackStage.BuildingVFS, 0, config.Files.Count, "Building VFS...");

                _vfsBuilder.Clear();
                foreach (var file in config.Files)
                {
                    _vfsBuilder.AddFile(file);
                }

                var vfsResult = _vfsBuilder.Build();

                cancellationToken.ThrowIfCancellationRequested();

                // Stage 4: Compress data
                ReportProgress(progressCallback, PackStage.Compressing, 0, config.Files.Count, "Compression complete.");

                // Stage 5: Modify PE
                ReportProgress(progressCallback, PackStage.ModifyingPE, 0, config.Files.Count, "Modifying PE structure...");

                // Select Loader DLL based on architecture
                var loaderData = SelectLoaderDll(peInfo.Architecture);
                if (loaderData == null)
                    return new PackResult { IsSuccess = false, ErrorMessage = $"Unsupported architecture: {peInfo.Architecture}. Only x86 and x64 are supported.", ErrorCode = PackErrorCode.UnsupportedArch };

                // Combine VFS data + Loader DLL into section data
                // (PeTool's stub generator prepends entry point stub + metadata)
                var sectionData = CombineSectionData(vfsResult, loaderData);

                // Apply PE modifications via PeTool DLL
                var peError = ApplyPeModifications(config.SourceExePath, config.OutputPath, sectionData);
                if (peError != null)
                {
                    var err = peError.Value;
                    return new PackResult { IsSuccess = false, ErrorMessage = err.ErrorMessage!, ErrorCode = err.ErrorCode };
                }

                cancellationToken.ThrowIfCancellationRequested();

                // Stage 6: Write output
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
                return new PackResult { IsSuccess = false, ErrorMessage = "Operation cancelled.", ErrorCode = PackErrorCode.OperationCancelled };
            }
            catch (PackException ex)
            {
                CleanupTempFile(tempOutputPath);
                return new PackResult { IsSuccess = false, ErrorMessage = $"Pack error ({ex.ErrorCode}): {ex.Message}" };
            }
            catch (Exception ex)
            {
                CleanupTempFile(tempOutputPath);
                return new PackResult { IsSuccess = false, ErrorMessage = $"Unexpected error: {ex.Message}", ErrorCode = PackErrorCode.UnexpectedError };
            }
        }

        private static string? ValidateInput(PackConfiguration config)
        {
            if (string.IsNullOrWhiteSpace(config.SourceExePath))
                return "Source EXE path is not specified.";

            if (!File.Exists(config.SourceExePath))
                return $"Source EXE file not found: {config.SourceExePath}";

            if (string.IsNullOrWhiteSpace(config.OutputPath))
                return "Output path is not specified.";

            try
            {
                var outputDir = Path.GetDirectoryName(config.OutputPath);
                if (!string.IsNullOrEmpty(outputDir) && !Directory.Exists(outputDir))
                    Directory.CreateDirectory(outputDir);
            }
            catch
            {
                return $"Output path is not writable: {config.OutputPath}";
            }

            foreach (var file in config.Files)
            {
                if (!File.Exists(file.SourcePath))
                    return $"Dependency file not found: {file.SourcePath}";
            }

            return null;
        }

        private static PeInfo? ParsePeInfo(string exePath)
        {
            try
            {
                using var stream = File.OpenRead(exePath);
                using var reader = new BinaryReader(stream);

                // Check DOS signature
                if (reader.ReadUInt16() != 0x5A4D) // 'MZ'
                    return null;

                // Read e_lfanew
                stream.Position = 0x3C;
                var peOffset = reader.ReadInt32();

                // Check PE signature
                stream.Position = peOffset;
                if (reader.ReadUInt32() != 0x00004550) // 'PE\0\0'
                    return null;

                // Read IMAGE_FILE_HEADER
                var machine = reader.ReadUInt16();
                var numberOfSections = reader.ReadUInt16();
                reader.ReadUInt32(); // TimeDateStamp
                reader.ReadUInt32(); // PointerToSymbolTable
                reader.ReadUInt32(); // NumberOfSymbols
                var sizeOfOptionalHeader = reader.ReadUInt16();
                reader.ReadUInt16(); // Characteristics

                // Read optional header
                var optionalHeaderOffset = stream.Position;
                var magic = reader.ReadUInt16();
                var is64Bit = magic == 0x20B; // PE32+

                uint entryPointRva;
                uint sizeOfImage;
                uint sizeOfHeaders;

                if (is64Bit)
                {
                    reader.ReadByte();  // MajorLinkerVersion
                    reader.ReadByte();  // MinorLinkerVersion
                    reader.ReadUInt32(); // SizeOfCode
                    reader.ReadUInt32(); // SizeOfInitializedData
                    reader.ReadUInt32(); // SizeOfUninitializedData
                    entryPointRva = reader.ReadUInt32();
                    reader.ReadUInt32(); // BaseOfCode
                    reader.ReadUInt64(); // ImageBase
                    reader.ReadUInt32(); // SectionAlignment
                    var fileAlignment = reader.ReadUInt32();
                    reader.ReadUInt16(); // MajorOperatingSystemVersion
                    reader.ReadUInt16(); // MinorOperatingSystemVersion
                    reader.ReadUInt16(); // MajorImageVersion
                    reader.ReadUInt16(); // MinorImageVersion
                    reader.ReadUInt16(); // MajorSubsystemVersion
                    reader.ReadUInt16(); // MinorSubsystemVersion
                    reader.ReadUInt32(); // Win32VersionValue
                    sizeOfImage = reader.ReadUInt32();
                    sizeOfHeaders = reader.ReadUInt32();
                }
                else
                {
                    reader.ReadByte();  // MajorLinkerVersion
                    reader.ReadByte();  // MinorLinkerVersion
                    reader.ReadUInt32(); // SizeOfCode
                    reader.ReadUInt32(); // SizeOfInitializedData
                    reader.ReadUInt32(); // SizeOfUninitializedData
                    entryPointRva = reader.ReadUInt32();
                    reader.ReadUInt32(); // BaseOfCode
                    reader.ReadUInt32(); // BaseOfData
                    reader.ReadUInt32(); // ImageBase
                    reader.ReadUInt32(); // SectionAlignment
                    var fileAlignment = reader.ReadUInt32();
                    reader.ReadUInt16(); // MajorOperatingSystemVersion
                    reader.ReadUInt16(); // MinorOperatingSystemVersion
                    reader.ReadUInt16(); // MajorImageVersion
                    reader.ReadUInt16(); // MinorImageVersion
                    reader.ReadUInt16(); // MajorSubsystemVersion
                    reader.ReadUInt16(); // MinorSubsystemVersion
                    reader.ReadUInt32(); // Win32VersionValue
                    sizeOfImage = reader.ReadUInt32();
                    sizeOfHeaders = reader.ReadUInt32();
                }

                var arch = machine switch
                {
                    0x014C => PeArchitecture.X86,
                    0x8664 => PeArchitecture.X64,
                    _ => PeArchitecture.Unknown
                };

                return new PeInfo
                {
                    Architecture = arch,
                    EntryPointRva = entryPointRva,
                    NumberOfSections = numberOfSections,
                    SizeOfImage = sizeOfImage,
                    SizeOfHeaders = sizeOfHeaders
                };
            }
            catch
            {
                return null;
            }
        }

        private static byte[]? SelectLoaderDll(PeArchitecture architecture)
        {
            string resourceName = architecture switch
            {
                PeArchitecture.X86 => "EniBox.Loader.x86.dll",
                PeArchitecture.X64 => "EniBox.Loader.x64.dll",
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

            // Write VFS metadata
            writer.Write(vfsResult.Metadata);

            // Write VFS data region
            writer.Write(vfsResult.DataRegion);

            // Write Loader DLL
            writer.Write(loaderData);

            return ms.ToArray();
        }

        private static (string? ErrorMessage, int ErrorCode)? ApplyPeModifications(string sourcePath, string outputPath, byte[] sectionData)
        {
            IntPtr ctx = IntPtr.Zero;
            try
            {
                // Open the source PE file
                int result = PeToolInterop.Open(sourcePath, out ctx);
                if (result != 0 || ctx == IntPtr.Zero)
                    return ($"Failed to open PE file (error {result}).", PackErrorCode.InvalidPe);

                // Add the .enibox section with VFS + Loader data
                const uint SECTION_CHARACTERISTICS = 0xC0000040;
                // IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE
                result = PeToolInterop.AddSection(ctx, ".enibox", sectionData, SECTION_CHARACTERISTICS);
                if (result != 0)
                    return ($"Failed to add .enibox section (error {result}).", PackErrorCode.SectionFull);

                // Process TLS callbacks to ensure Loader initializes before TLS
                result = PeToolInterop.ProcessTLS(ctx);
                if (result != 0)
                    return ($"Failed to process TLS callbacks (error {result}).", PackErrorCode.ImportMergeFailed);

                // Merge Loader DLL into import table so it loads before entry point
                result = PeToolInterop.MergeImports(ctx, IntPtr.Zero, 0);
                if (result != 0)
                    return ($"Failed to merge imports (error {result}).", PackErrorCode.ImportMergeFailed);

                result = PeToolInterop.Save(ctx, outputPath);
                if (result != 0)
                    return ($"Failed to save modified PE (error {result}).", PackErrorCode.WriteFailed);

                return null;
            }
            catch (DllNotFoundException ex)
            {
                return ($"PeTool DLL not found: {ex.Message}. Ensure EniBox.PeTool.dll is in the application directory.", PackErrorCode.LoaderNotFound);
            }
            catch (Exception ex)
            {
                return ($"PE modification failed: {ex.Message}", PackErrorCode.UnexpectedError);
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
                try { File.Delete(path); } catch { }
            }
        }
    }
}
