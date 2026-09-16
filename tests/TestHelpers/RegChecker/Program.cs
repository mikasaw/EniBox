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

    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegDeleteValueA(IntPtr hKey, string lpValueName);

    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegDeleteKeyA(IntPtr hKey, string lpSubKey);

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

        // 同键第二预置值（多值聚合回归检查，见 E2E_MultiValueSameKey）
        if (RegOpenKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest", 0, KEY_READ, out IntPtr hSecond) == 0)
        {
            var second = new StringBuilder(256);
            uint ssize = 256, stype = 0;
            int src = RegQueryValueExA(hSecond, "SecondValue", IntPtr.Zero, out stype, second, ref ssize);
            RegCloseKey(hSecond);
            Console.WriteLine(src == 0 ? $"CHECK:REG_SECOND:OK:{second}" : "CHECK:REG_SECOND:MISS");
        }
        else
        {
            Console.WriteLine("CHECK:REG_SECOND:MISS");
        }
        return 0;
    }

    /// <summary>del 模式：删除 Runtime\PersistValue（验证删除值 + 持久化）</summary>
    static int DeletePersistValue()
    {
        if (RegOpenKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime", 0, KEY_WRITE, out IntPtr hKey) != 0)
        {
            Console.WriteLine($"CHECK:REG_DEL:FAIL:OpenKeyEx rc={Marshal.GetLastWin32Error()}");
            return 1;
        }
        int rc = RegDeleteValueA(hKey, "PersistValue");
        RegCloseKey(hKey);
        Console.WriteLine(rc == 0 ? "CHECK:REG_DEL:OK" : $"CHECK:REG_DEL:FAIL:rc={rc}");
        return rc == 0 ? 0 : 1;
    }

    /// <summary>sub 模式：在 Runtime 下再建 Sub 子键并写值（验证孙键 + 删除键的子键守卫）</summary>
    static int WriteSubKeyValue(string payload)
    {
        int rc = RegCreateKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime\Sub",
            0, IntPtr.Zero, 0, KEY_WRITE, IntPtr.Zero, out IntPtr hKey, out uint _);
        if (rc != 0)
        {
            Console.WriteLine($"CHECK:REG_SUB:FAIL:CreateKeyEx rc={rc}");
            return 1;
        }
        var bytes = Encoding.ASCII.GetBytes(payload + "\0");
        rc = RegSetValueExA(hKey, "SubValue", 0, REG_SZ, bytes, (uint)bytes.Length);
        RegCloseKey(hKey);
        Console.WriteLine(rc == 0 ? $"CHECK:REG_SUB:OK:{payload}" : $"CHECK:REG_SUB:FAIL:rc={rc}");
        return rc == 0 ? 0 : 1;
    }

    /// <summary>delsub 模式：删除 Runtime\Sub 键</summary>
    static int DeleteSubKey()
    {
        int rc = RegDeleteKeyA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime\Sub");
        Console.WriteLine(rc == 0 ? "CHECK:REG_DELSUB:OK" : $"CHECK:REG_DELSUB:FAIL:rc={rc}");
        return rc == 0 ? 0 : 1;
    }

    /// <summary>delkey 模式：删除 Runtime 键（有 Sub 子键时应失败 ERROR_ACCESS_DENIED=5）</summary>
    static int DeleteRuntimeKey()
    {
        int rc = RegDeleteKeyA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime");
        Console.WriteLine(rc == 0 ? "CHECK:REG_DELKEY:OK" : $"CHECK:REG_DELKEY:FAIL:rc={rc}");
        return rc == 0 ? 0 : 1;
    }

    /// <summary>delroot 模式：删除作用域根 EniBoxTest（应被拒绝 rc=5，防子树逃逸）</summary>
    static int DeleteScopeRoot()
    {
        int rc = RegDeleteKeyA(HKEY_CURRENT_USER, @"Software\EniBoxTest");
        Console.WriteLine(rc == 0 ? "CHECK:REG_DELROOT:OK" : $"CHECK:REG_DELROOT:FAIL:rc={rc}");
        return rc == 0 ? 0 : 1;
    }

    /// <summary>delopen 模式：持有 Sub 键句柄时删除它，再查询旧句柄（应为 ERROR_KEY_DELETED=1016）</summary>
    static int DeleteThenUseStaleHandle()
    {
        if (RegOpenKeyExA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime\Sub", 0, KEY_READ, out IntPtr hKey) != 0)
        {
            Console.WriteLine("CHECK:REG_DELOPEN:FAIL:OpenKeyEx failed");
            return 1;
        }
        int delRc = RegDeleteKeyA(HKEY_CURRENT_USER, @"Software\EniBoxTest\Runtime\Sub");
        var buf = new StringBuilder(64);
        uint size = 64, type = 0;
        int queryRc = RegQueryValueExA(hKey, "SubValue", IntPtr.Zero, out type, buf, ref size);
        RegCloseKey(hKey);
        const int ERROR_KEY_DELETED = 1018; /* winerror.h: ERROR_KEY_DELETED = 1018L */
        bool ok = delRc == 0 && queryRc == ERROR_KEY_DELETED;
        Console.WriteLine(ok
            ? "CHECK:REG_DELOPEN:OK:1018"
            : $"CHECK:REG_DELOPEN:FAIL:del={delRc} query={queryRc} handle=0x{hKey.ToInt64():X}");
        return ok ? 0 : 1;
    }

    static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "write")
            return WritePersistValue(args[1]);
        if (args.Length == 1 && args[0] == "read")
            return ReadPersistValue();
        if (args.Length == 1 && args[0] == "del")
            return DeletePersistValue();
        if (args.Length == 2 && args[0] == "sub")
            return WriteSubKeyValue(args[1]);
        if (args.Length == 1 && args[0] == "delsub")
            return DeleteSubKey();
        if (args.Length == 1 && args[0] == "delkey")
            return DeleteRuntimeKey();
        if (args.Length == 1 && args[0] == "delroot")
            return DeleteScopeRoot();
        if (args.Length == 1 && args[0] == "delopen")
            return DeleteThenUseStaleHandle();

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
