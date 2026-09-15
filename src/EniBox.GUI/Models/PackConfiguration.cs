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
        /// <summary>
        /// 注册表虚拟化预置值（仅在 EnableRegistryVirtualization=true 时生效）。
        /// 语义：预置键可读；对虚拟句柄的写入仅进程内有效（不持久化）。
        /// </summary>
        public List<PackRegistryValue> RegistryValues { get; set; } = new();
    }
}
