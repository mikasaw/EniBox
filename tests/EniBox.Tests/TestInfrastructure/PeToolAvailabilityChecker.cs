using System;
using System.Runtime.InteropServices;

namespace EniBox.Tests.TestInfrastructure;

public static class PeToolAvailabilityChecker
{
    private const string DllName = "EniBox.PeTool.dll";
    private const string ProbeEntry = "PE_Open";

    private static readonly Lazy<bool> _isAvailable = new(TryLoadPeTool);

    public static bool IsAvailable => _isAvailable.Value;

    private static bool TryLoadPeTool()
    {
        try
        {
            // 验证 1: 管理类型 metadata 可用 (防止 ImportEntry struct 缺失)
            var size = Marshal.SizeOf<EniBox.GUI.Interop.PeToolInterop.ImportEntry>();
            if (size <= 0) return false;

            // 验证 2: native DLL 实际可加载 (防止文件缺失 / 损坏)
            if (!NativeLibrary.TryLoad(DllName, out var handle)) return false;
            try
            {
                // 验证 3: 关键导出函数存在 (防止版本不匹配 / 符号错)
                if (!NativeLibrary.TryGetExport(handle, ProbeEntry, out _)) return false;
            }
            finally
            {
                NativeLibrary.Free(handle);
            }

            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }
}
