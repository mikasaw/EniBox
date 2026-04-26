using System;
using System.Windows;
using EniBox.GUI.ViewModels;

namespace EniBox.GUI
{
    public partial class App : Application
    {
        private void Application_Startup(object sender, StartupEventArgs e)
        {
            // Check for CLI mode
            if (e.Args.Length > 0 && Array.Exists(e.Args, a => a == "--cli"))
            {
                // CLI mode - handled separately
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
