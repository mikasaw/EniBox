using EniBox.GUI.Services;
using Microsoft.Extensions.DependencyInjection;

namespace EniBox.GUI
{
    public static class ServiceCollectionExtensions
    {
        public static IServiceCollection AddEniBoxServices(this IServiceCollection services)
        {
            services.AddSingleton<ICompressor, LzmaCompressor>();
            services.AddSingleton<IVfsBuilder, VfsBuilder>();
            services.AddTransient<IPackService, PackService>();
            return services;
        }
    }
}
