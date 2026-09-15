using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public interface IVfsBuilder
    {
        void AddFile(PackFileItem file);
        void AddDirectory(string virtualPath);
        void AddRegistryValue(PackRegistryValue value);
        VfsBuildResult Build();
        void Clear();
    }
}
