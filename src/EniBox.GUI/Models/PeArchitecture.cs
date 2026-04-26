namespace EniBox.GUI.Models
{
    public enum PeArchitecture
    {
        Unknown = 0,
        X86 = 0x014C,    // IMAGE_FILE_MACHINE_I386
        X64 = 0x8664     // IMAGE_FILE_MACHINE_AMD64
    }
}
