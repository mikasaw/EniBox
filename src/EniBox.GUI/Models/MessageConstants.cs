namespace EniBox.GUI.Models
{
    public static class MessageConstants
    {
        public const string SourceExeNotSpecified = "Source EXE path is not specified.";
        public const string SourceExeNotFound = "Source EXE file not found: ";
        public const string OutputNotSpecified = "Output path is not specified.";
        public const string OutputNotWritable = "Output path is not writable: ";
        public const string DependencyNotFound = "Dependency file not found: ";
        public const string FailedParsePe = "Failed to parse source EXE PE structure.";
        public const string UnsupportedArch = "Unsupported architecture: ";
        public const string ArchSupportSuffix = ". Only x86 and x64 are supported.";
        public const string FailedOpenPe = "Failed to open PE file (error ";
        public const string FailedAddSection = "Failed to add .enibox section (error ";
        public const string FailedProcessTls = "Failed to process TLS callbacks (error ";
        public const string FailedMergeImports = "Failed to merge imports (error ";
        public const string FailedSavePe = "Failed to save modified PE (error ";
        public const string PeToolNotFoundPrefix = "PeTool DLL not found: ";
        public const string PeToolNotFoundSuffix = ". Ensure EniBox.PeTool.dll is in the application directory.";
        public const string PeModificationFailed = "PE modification failed: ";
        public const string OperationCancelled = "Operation cancelled.";
        public const string PackErrorFormat = "Pack error ({0}): {1}";
        public const string UnexpectedError = "Unexpected error: ";
    }
}
