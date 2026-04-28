using System;
using System.Runtime.InteropServices;
using System.Text;

class Program
{
    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegOpenKeyExA(IntPtr hKey, string lpSubKey, uint ulOptions, uint samDesired, out IntPtr phkResult);

    [DllImport("advapi32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern int RegQueryValueExA(IntPtr hKey, string lpValueName, IntPtr lpReserved, out uint lpType, StringBuilder lpData, ref uint lpcbData);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern int RegCloseKey(IntPtr hKey);

    static readonly IntPtr HKEY_CURRENT_USER = new IntPtr(unchecked((long)0x80000001));
    static readonly IntPtr HKEY_LOCAL_MACHINE = new IntPtr(unchecked((long)0x80000002));
    const uint KEY_READ = 0x20019;

    static int Main(string[] args)
    {
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
