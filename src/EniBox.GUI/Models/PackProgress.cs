namespace EniBox.GUI.Models
{
    public sealed class PackProgress
    {
        public int TotalFiles { get; init; }
        public int ProcessedFiles { get; set; }
        public string CurrentFile { get; set; } = string.Empty;
        public PackStage Stage { get; set; }
        public double ProgressPercent { get; set; }
    }

    public enum PackStage
    {
        CollectingFiles,
        BuildingVFS,
        Compressing,
        ModifyingPE,
        WritingOutput,
        Completed,
        Failed
    }
}
