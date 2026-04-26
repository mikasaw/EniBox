namespace EniBox.GUI.Models
{
    public sealed class PeInfo
    {
        public PeArchitecture Architecture { get; init; }
        public uint EntryPointRva { get; init; }
        public uint NumberOfSections { get; init; }
        public uint SizeOfImage { get; init; }
        public uint SizeOfHeaders { get; init; }
        public bool Is64Bit => Architecture == PeArchitecture.X64;
    }
}
