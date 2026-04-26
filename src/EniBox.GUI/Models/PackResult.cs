namespace EniBox.GUI.Models
{
    public sealed class PackResult
    {
        public bool IsSuccess { get; init; }
        public string OutputPath { get; init; } = string.Empty;
        public string ErrorMessage { get; init; } = string.Empty;
        public long OutputFileSize { get; init; }
    }
}
