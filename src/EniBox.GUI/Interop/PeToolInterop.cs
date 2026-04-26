using System;
using System.Runtime.InteropServices;
using EniBox.GUI.Models;

namespace EniBox.GUI.Interop
{
    public static class PeToolInterop
    {
        private const string DllName = "EniBox.PeTool.dll";

        [StructLayout(LayoutKind.Sequential)]
        private struct NativePeInfo
        {
            public uint Machine;
            public uint EntryPointRva;
            public uint NumberOfSections;
            public uint SizeOfImage;
            public uint SizeOfHeaders;
        }

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        private static extern int PE_Open(string pePath, out IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int PE_GetInfo(IntPtr ctx, out NativePeInfo info);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        private static extern int PE_AddSection(IntPtr ctx, string name, byte[] data, uint dataSize, uint characteristics);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int PE_SetEntryPoint(IntPtr ctx, uint newEntryRva);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int PE_MergeImports(IntPtr ctx, IntPtr entries, uint count);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern int PE_ProcessTLS(IntPtr ctx);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        private static extern int PE_Save(IntPtr ctx, string outputPath);

        [DllImport(DllName, CallingConvention = CallingConvention.StdCall)]
        private static extern void PE_Close(IntPtr ctx);

        public static int Open(string pePath, out IntPtr ctx)
        {
            return PE_Open(pePath, out ctx);
        }

        public static PeInfo? GetInfo(IntPtr ctx)
        {
            int result = PE_GetInfo(ctx, out var nativeInfo);
            if (result != 0)
                return null;

            return new PeInfo
            {
                Architecture = nativeInfo.Machine switch
                {
                    0x014C => PeArchitecture.X86,
                    0x8664 => PeArchitecture.X64,
                    _ => PeArchitecture.Unknown
                },
                EntryPointRva = nativeInfo.EntryPointRva,
                NumberOfSections = nativeInfo.NumberOfSections,
                SizeOfImage = nativeInfo.SizeOfImage,
                SizeOfHeaders = nativeInfo.SizeOfHeaders
            };
        }

        public static int AddSection(IntPtr ctx, string name, byte[] data, uint characteristics)
        {
            return PE_AddSection(ctx, name, data, (uint)data.Length, characteristics);
        }

        public static int SetEntryPoint(IntPtr ctx, uint newEntryRva)
        {
            return PE_SetEntryPoint(ctx, newEntryRva);
        }

        public static int Save(IntPtr ctx, string outputPath)
        {
            return PE_Save(ctx, outputPath);
        }

        public static void Close(IntPtr ctx)
        {
            PE_Close(ctx);
        }
    }
}
