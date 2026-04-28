using System;

namespace EniBox.Tests.TestInfrastructure;

public static class PeToolAvailabilityChecker
{
    private static readonly Lazy<bool> _isAvailable = new(TryLoadPeTool);
    
    public static bool IsAvailable => _isAvailable.Value;
    
    private static bool TryLoadPeTool()
    {
        try
        {
            var size = System.Runtime.InteropServices.Marshal.SizeOf<EniBox.GUI.Interop.PeToolInterop.ImportEntry>();
            return size > 0;
        }
        catch (Exception)
        {
            return false;
        }
    }
}
