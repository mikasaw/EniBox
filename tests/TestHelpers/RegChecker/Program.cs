using System;
using System.Runtime.InteropServices;
using System.Text;

class Program
{
    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegOpenKeyExA(IntPtr hKey, string lpSubKey, uint ulOptions, uint samDesired, out IntPtr phkResult);

    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegCreateKeyExA(IntPtr hKey, string lpSubKey, uint reserved, IntPtr lpClass, uint dwOptions, uint samDesired, IntPtr lpSecurityAttributes, out IntPtr phkResult, out uint lpdwDisposition);

    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegQueryValueExA(IntPtr hKey, string lpValueName, IntPtr lpReserved, out uint lpType, StringBuilder lpData, ref uint lpcbData);

    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegSetValueExA(IntPtr hKey, string lpValueName, uint reserved, uint dwType, byte[] lpData, uint cbData);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern int RegCloseKey(IntPtr hKey);

    static readonly IntPtr HKEY_CURRENT_USER = new IntPtr(unchecked((long)0x80000001));
    static readonly IntPtr HKEY_LOCAL_MACHINE = new IntPtr(unchecked((long)0x80000002));
    const uint KEY_READ = 0x20019;
    const uint KEY_WRITE = 0x20006;
    const uint REG_SZ = 1;

    /// <summary>
    /// write 模式：在预置键 HKCU\Software\EniBoxTest 的作用域子树内新建
    /// Runtime 子键并写入 PersistValue（验证子树规则 + 持久化落盘）。
    /// </summary>
    static int WritePersistValue(string payload)
    {
        int rc = RegCreateKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime",
            0, IntPtr.Zero, 0, KEY_WRITE, IntPtr.Zero, out IntPtr hKey, out uint disp);
        if (rc != 0)
        {
            Console.WriteLine($"CHECK:REG_WRITE:FAIL:CreateKeyEx rc={rc}");
            return 1;
        }
        var bytes = Encoding.ASCII.GetBytes(payload + "\0");
        rc = RegSetValueExA(hKey, "PersistValue", 0, REG_SZ, bytes, (uint)bytes.Length);
        RegCloseKey(hKey);
        Console.WriteLine(rc == 0
            ? $"CHECK:REG_WRITE:OK:{payload} disp={disp}"
            : $"CHECK:REG_WRITE:FAIL:SetValueEx rc={rc}");
        return rc == 0 ? 0 : 1;
    }

    /// <summary>
    /// read 模式：读回 Runtime\PersistValue（MISS = 未持久化或已重置），
    /// 同时校验预置键 TestValue 仍可读（sidecar 整库替换不应丢失预置内容）。
    /// </summary>
    static int ReadPersistValue()
    {
        if (RegOpenKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime", 0, KEY_READ, out IntPtr hKey) != 0)
        {
            Console.WriteLine("CHECK:REG_PERSIST:MISS");
        }
        else
        {
            var value = new StringBuilder(512);
            uint size = 512, type = 0;
            int rc = RegQueryValueExA(hKey, "PersistValue", IntPtr.Zero, out type, value, ref size);
            RegCloseKey(hKey);
            Console.WriteLine(rc == 0 ? $"CHECK:REG_PERSIST:OK:{value}" : "CHECK:REG_PERSIST:MISS");
        }

        if (RegOpenKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest", 0, KEY_READ, out IntPtr hPreset) == 0)
        {
            var preset = new StringBuilder(256);
            uint psize = 256, ptype = 0;
            int prc = RegQueryValueExA(hPreset, "TestValue", IntPtr.Zero, out ptype, preset, ref psize);
            RegCloseKey(hPreset);
            Console.WriteLine(prc == 0 ? $"CHECK:REG_PRESET:OK:{preset}" : "CHECK:REG_PRESET:MISS");
        }
        else
        {
            Console.WriteLine("CHECK:REG_PRESET:MISS");
        }
        return 0;
    }

    static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "write")
            return WritePersistValue(args[1]);
        if (args.Length == 1 && args[0] == "read")
            return ReadPersistValue();

        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\EniBoxTest", 0, KEY_READ, out IntPtr hKey) == 0)
        {
            var value = new StringBuilder(256);
            uint size = 256;
            uint type = 0;
            if (RegQueryValueExA(hKey, "TestValue", IntPtr.Zero, out type, value, ref size) == 0)
            {
                Console.WriteLine($"CHECK:REG_VIRTUAL:OK:Software\\EniBoxTest\\TestValue = {value}");
            }
            else
            {
                Console.WriteLine("CHECK:REG_VIRTUAL:FAIL:QueryValueEx failed");
            }
            RegCloseKey(hKey);
        }
        else
        {
            Console.WriteLine("CHECK:REG_VIRTUAL:FAIL:OpenKeyEx failed");
        }

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, out IntPtr hReal) == 0)
        {
            var productName = new StringBuilder(256);
            uint size = 256;
            uint type = 0;
            if (RegQueryValueExA(hReal, "ProductName", IntPtr.Zero, out type, productName, ref size) == 0)
            {
                Console.WriteLine($"CHECK:REG_REAL:OK:ProductName = {productName}");
            }
            else
            {
                Console.WriteLine("CHECK:REG_REAL:FAIL:QueryValueEx failed");
            }
            RegCloseKey(hReal);
        }
        else
        {
            Console.WriteLine("CHECK:REG_REAL:FAIL:OpenKeyEx failed");
        }

        return 0;
    }
}
