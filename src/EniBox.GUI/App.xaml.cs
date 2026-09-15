using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;
using EniBox.GUI.Services;
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

        public App()
        {
            // D8 全局异常兜底：三级钩子全部落文件日志（%LOCALAPPDATA%\EniBox\logs），
            // 保证用户遇到异常时留下可诊断的现场。
            DispatcherUnhandledException += OnDispatcherUnhandledException;
            AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;
            TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
        }

        private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            ErrorLog.Write("WPF.Dispatcher", e.Exception);
            MessageBox.Show(
                $"发生未处理的异常：{e.Exception.Message}\n\n详细信息已写入 {ErrorLog.LogDirectory}",
                "EniBox", MessageBoxButton.OK, MessageBoxImage.Error);
            // UI 线程异常默认可恢复：标记 Handled 让应用继续运行
            e.Handled = true;
        }

        private void OnDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
        {
            ErrorLog.Write("AppDomain",
                e.ExceptionObject as Exception ?? new Exception(e.ExceptionObject?.ToString()));
            // IsTerminating=true 时进程即将退出，这里只负责留日志
        }

        private void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
        {
            ErrorLog.Write("TaskScheduler", e.Exception);
            e.SetObserved();
        }

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
