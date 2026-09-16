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
        /// 语义：预置键路径即作用域根，整个子树（含运行时新建键）虚拟化；
        /// 写入隔离于真实注册表之外，并持久化到产物同目录 &lt;产物&gt;.vreg.bin
        /// （删除该文件即重置为预置值）。
        /// </summary>
        public List<PackRegistryValue> RegistryValues { get; set; } = new();
    }
}
