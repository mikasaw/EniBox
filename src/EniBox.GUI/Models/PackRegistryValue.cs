using System;

namespace EniBox.GUI.Models
{
    /// <summary>
    /// 注册表虚拟化的预置值：封包时写入 VFS，Loader 在进程初始化时预载。
    /// 范围语义：预置键路径即作用域根，整个子树（含运行时新建键）虚拟化；
    /// 写入不触碰真实注册表，并持久化到产物同目录 &lt;产物&gt;.vreg.bin；
    /// sidecar 存在时整体替换预置值（预置仅首启生效）；作用域外的键完全
    /// 透传真实注册表。
    /// </summary>
    public sealed class PackRegistryValue
    {
        public const uint REG_SZ = 1;
        public const uint REG_EXPAND_SZ = 2;
        public const uint REG_BINARY = 3;
        public const uint REG_DWORD = 4;
        public const uint REG_MULTI_SZ = 7;

        /// <summary>完整键路径，含根键名，如 HKEY_CURRENT_USER\Software\Vendor\App</summary>
        public string KeyPath { get; set; } = string.Empty;
        /// <summary>值名称（默认值用空字符串）</summary>
        public string ValueName { get; set; } = string.Empty;
        /// <summary>REG_* 类型常量</summary>
        public uint Type { get; set; }
        /// <summary>原始数据。REG_SZ/REG_EXPAND_SZ 为 UTF-16LE 字节（含 NUL）；REG_DWORD 为 4 字节小端</summary>
        public byte[] Data { get; set; } = System.Array.Empty<byte>();

        public static PackRegistryValue FromString(string keyPath, string valueName, string value)
        {
            var utf16 = System.Text.Encoding.Unicode.GetBytes(value + "\0");
            return new PackRegistryValue
            {
                KeyPath = keyPath,
                ValueName = valueName,
                Type = REG_SZ,
                Data = utf16
            };
        }

        public static PackRegistryValue FromDword(string keyPath, string valueName, uint value)
        {
            return new PackRegistryValue
            {
                KeyPath = keyPath,
                ValueName = valueName,
                Type = REG_DWORD,
                Data = BitConverter.GetBytes(value)
            };
        }
    }
}
