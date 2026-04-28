using System;
using System.Runtime.InteropServices;
using System.Windows;
using EniBox.GUI.ViewModels;
using Microsoft.Extensions.DependencyInjection;

namespace EniBox.GUI
{
    public partial class App : Application
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AttachConsole(int dwProcessId);

        private const int ATTACH_PARENT_PROCESS = -1;

        public static IServiceProvider Services { get; private set; } = null!;

        private void Application_Startup(object sender, StartupEventArgs e)
        {
            var services = new ServiceCollection();
            services.AddEniBoxServices();
            Services = services.BuildServiceProvider();

            if (e.Args.Length > 0 && Array.Exists(e.Args, a => a == "--cli"))
            {
                AttachConsole(ATTACH_PARENT_PROCESS);

                var cli = new Services.CliRunner(Services);
                int exitCode = cli.Run(e.Args);
                Shutdown(exitCode);
                return;
            }

            var packService = Services.GetRequiredService<Services.IPackService>();
            var mainWindow = new Views.MainWindow
            {
                DataContext = new MainViewModel(packService)
            };
            mainWindow.Show();
        }
    }
}
