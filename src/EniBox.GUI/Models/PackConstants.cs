namespace EniBox.GUI.Models
{
    public static class PackConstants
    {
        public const string EniboxSectionName = ".enibox";
        // RWX: the bootstrap code at the section start executes (entry point is
        // redirected here; GUARD_CF is cleared so the redirect is allowed) and
        // the Loader's DllMain patches the VA placeholder in place.
        public const uint EniboxSectionCharacteristics = 0xE0000040;

        // .enibox 引导区保留字段 [272..275]：打包配置位标志（Loader DllMain 读取）。
        // 契约三方同步：PackConstants（写）↔ loader_main.c（读）↔ vfs_link.h（子进程继承）。
        public const int ConfigFlagsOffset = 272;
        public const uint ConfigFlagSubprocessInjection = 0x1;
        public const uint ConfigFlagRegistryVirtualization = 0x2;
        public const string LoaderDllImportName = "EniBox.Loader.dll";
        public const string LoaderDllX64Resource = "EniBox.Loader.x64.dll";
        public const string LoaderDllX86Resource = "EniBox.Loader.x86.dll";
    }
}
