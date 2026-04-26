using EniBox.GUI.Models;

namespace EniBox.GUI.Services
{
    public interface IVfsBuilder
    {
        void AddFile(PackFileItem file);
        void AddDirectory(string virtualPath);
        VfsBuildResult Build();
        void Clear();
    }
}
