using System;
using System.Runtime.InteropServices;
using System.Text;

class Program
{
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    static extern IntPtr CreateFileA(string lpFileName, uint dwDesiredAccess, uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadFile(IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToRead, out uint lpNumberOfBytesRead, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr hObject);

    const uint GENERIC_READ = 0x80000000;
    const uint FILE_SHARE_READ = 0x00000001;
    const uint OPEN_EXISTING = 3;
    static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

    static int Main(string[] args)
    {
        var testFile = args.Length >= 1 ? args[0] : "test_data.txt";

        var hFile = CreateFileA(testFile, GENERIC_READ, FILE_SHARE_READ, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            var buf = new byte[4096];
            if (ReadFile(hFile, buf, (uint)buf.Length, out uint read, IntPtr.Zero) && read > 0)
            {
                var text = Encoding.UTF8.GetString(buf, 0, (int)Math.Min(read, 100));
                Console.WriteLine($"CHECK:CHILD_VFS:OK:{testFile} ({read} bytes, preview: {text.Trim()})");
            }
            else
            {
                Console.WriteLine($"CHECK:CHILD_VFS:FAIL:{testFile} (0 bytes)");
            }
            CloseHandle(hFile);
            return 0;
        }
        else
        {
            int err = Marshal.GetLastWin32Error();
            Console.WriteLine($"CHECK:CHILD_VFS:FAIL:{testFile} (error={err})");
            return 1;
        }
    }
}
