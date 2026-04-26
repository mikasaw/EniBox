namespace EniBox.GUI.Models
{
    /// <summary>
    /// Unified error codes for EniBox, aligned with PeTool C error codes.
    /// Range allocation:
    ///   1000-1999: File I/O errors
    ///   2000-2999: PE format errors
    ///   3000-3999: VFS errors
    ///   4000-4999: Compression errors
    ///   5000-5999: PE modification errors
    ///   6000-6999: Loader errors
    ///   9000-9999: General errors
    /// </summary>
    public static class PackErrorCode
    {
        // File I/O errors (1000-1999) - aligned with PeTool
        public const int FileNotFound     = 1001;
        public const int ReadFailed       = 1002;
        public const int WriteFailed      = 1003;

        // PE format errors (2000-2999) - aligned with PeTool
        public const int InvalidPe        = 2001;
        public const int UnsupportedArch  = 2002;
        public const int InvalidDosHeader = 2003;
        public const int InvalidPeHeader  = 2004;

        // VFS errors (3000-3999)
        public const int VfsBuildFailed   = 3001;
        public const int VfsInvalidData   = 3002;
        public const int VfsChecksumMismatch = 3003;

        // Compression errors (4000-4999)
        public const int CompressionFailed   = 4001;
        public const int DecompressionFailed = 4002;

        // PE modification errors (5000-5999) - aligned with PeTool
        public const int SectionFull       = 5001;
        public const int ImportMergeFailed = 5002;
        public const int NoMemory          = 5005;

        // Loader errors (6000-6999)
        public const int LoaderNotFound    = 6001;
        public const int LoaderInitFailed  = 6002;
        public const int LoaderHookFailed  = 6003;

        // General errors (9000-9999)
        public const int OperationCancelled = 9001;
        public const int ValidationFailed   = 9002;
        public const int UnexpectedError    = 9999;
    }
}
