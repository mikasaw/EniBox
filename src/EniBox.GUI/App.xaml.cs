using System;
using System.Runtime.InteropServices;
using System.Windows;
using EniBox.GUI.ViewModels;

namespace EniBox.GUI
{
    public partial class App : Application
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AttachConsole(int dwProcessId);

        private const int ATTACH_PARENT_PROCESS = -1;

        private void Application_Startup(object sender, StartupEventArgs e)
        {
            // Check for CLI mode
            if (e.Args.Length > 0 && Array.Exists(e.Args, a => a == "--cli"))
            {
                // Attach to parent console for CLI output
                AttachConsole(ATTACH_PARENT_PROCESS);

                var cli = new Services.CliRunner();
                int exitCode = cli.Run(e.Args);
                Shutdown(exitCode);
                return;
            }

            // GUI mode
            var mainWindow = new Views.MainWindow
            {
                DataContext = new MainViewModel()
            };
            mainWindow.Show();
        }
    }
}
