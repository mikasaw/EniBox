using System.Collections.Generic;

namespace EniBox.GUI.Models
{
    public sealed class PackConfiguration
    {
        public string SourceExePath { get; set; } = string.Empty;
        public string OutputPath { get; set; } = string.Empty;
        public List<PackFileItem> Files { get; set; } = new();
        public bool EnableRegistryVirtualization { get; set; }
        public bool EnableSubProcessInjection { get; set; } = true;
    }
}
