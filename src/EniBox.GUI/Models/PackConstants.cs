namespace EniBox.GUI.Models
{
    public static class PackConstants
    {
        public const string EniboxSectionName = ".enibox";
        // RWX: the bootstrap code at the section start executes (entry point is
        // redirected here; GUARD_CF is cleared so the redirect is allowed) and
        // the Loader's DllMain patches the VA placeholder in place.
        public const uint EniboxSectionCharacteristics = 0xE0000040;
        public const string LoaderDllImportName = "EniBox.Loader.dll";
        public const string LoaderDllX64Resource = "EniBox.Loader.x64.dll";
        public const string LoaderDllX86Resource = "EniBox.Loader.x86.dll";
    }
}
