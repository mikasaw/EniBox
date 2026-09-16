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

        /// <summary>REG_EXPAND_SZ 预置值（数据为 UTF-16LE 含 NUL，与 FromString 同构）</summary>
        public static PackRegistryValue FromExpandString(string keyPath, string valueName, string value)
        {
            var utf16 = System.Text.Encoding.Unicode.GetBytes(value + "\0");
            return new PackRegistryValue
            {
                KeyPath = keyPath,
                ValueName = valueName,
                Type = REG_EXPAND_SZ,
                Data = utf16
            };
        }

        /// <summary>REG_BINARY 预置值（原始字节）</summary>
        public static PackRegistryValue FromBinary(string keyPath, string valueName, byte[] data)
        {
            return new PackRegistryValue
            {
                KeyPath = keyPath,
                ValueName = valueName,
                Type = REG_BINARY,
                Data = (byte[])data.Clone()
            };
        }

        /// <summary>
        /// 解析 CLI 的 --registry-value 单条规格：KEY|NAME|TYPE|DATA 或 KEY|NAME|DATA（TYPE 缺省 SZ）。
        /// TYPE ∈ {SZ, EXPAND_SZ, DWORD, BINARY}；DWORD 为无符号十进制，BINARY 为十六进制串
        /// （空格/连字符可选分隔，如 "DE AD BE EF" 或 "DEADBEEF"）。MULTI_SZ 仅编程 API 支持。
        /// 限制：字段以 '|' 分隔，SZ/EXPAND_SZ 文本中无法包含 '|' 字符。
        /// 解析失败抛 ArgumentException。
        /// </summary>
        public static PackRegistryValue FromSpec(string spec)
        {
            var parts = spec.Split('|');
            if (parts.Length < 3 || parts.Length > 4)
                throw new ArgumentException(
                    $"--registry-value 格式应为 KEY|NAME|TYPE|DATA 或 KEY|NAME|DATA（{parts.Length} 段）");

            var key = parts[0].Trim();
            var name = parts[1].Trim();
            var typeToken = parts.Length == 4 ? parts[2].Trim().ToUpperInvariant() : "SZ";
            var dataToken = parts.Length == 4 ? parts[3] : parts[2];

            if (string.IsNullOrEmpty(key))
                throw new ArgumentException("KEY 不能为空（需含根键名，如 HKEY_CURRENT_USER\\Software\\App）");

            return typeToken switch
            {
                "SZ" => FromString(key, name, dataToken),
                "EXPAND_SZ" => FromExpandString(key, name, dataToken),
                "DWORD" => FromDword(key, name,
                    uint.TryParse(dataToken.Trim(), out var dw)
                        ? dw
                        : throw new ArgumentException($"DWORD 数据非法: '{dataToken}'（需无符号十进制）")),
                "BINARY" => FromBinary(key, name, ParseHex(dataToken)),
                _ => throw new ArgumentException($"TYPE 非法: '{typeToken}'（支持 SZ|EXPAND_SZ|DWORD|BINARY）")
            };
        }

        private static byte[] ParseHex(string text)
        {
            var compact = text.Trim().Replace(" ", "").Replace("-", "");
            if (compact.Length == 0 || compact.Length % 2 != 0)
                throw new ArgumentException($"BINARY 数据非法: '{text}'（需偶数长度十六进制）");
            var bytes = new byte[compact.Length / 2];
            for (int i = 0; i < bytes.Length; i++)
            {
                if (!byte.TryParse(compact.Substring(i * 2, 2),
                        System.Globalization.NumberStyles.HexNumber, null, out bytes[i]))
                    throw new ArgumentException($"BINARY 数据非法: '{text}'（含非十六进制字符）");
            }
            return bytes;
        }
    }
}
