#if !CLI_MODE
using System;
using System.Windows;

namespace EniBox.GUI
{
    /// <summary>
    /// 应用程序主入口点
    /// </summary>
    public class Program
    {
        /// <summary>
        /// 应用程序主入口点方法
        /// </summary>
        /// <param name="args">命令行参数</param>
        /// <returns>程序退出码（0表示成功，非0表示错误）</returns>
        [STAThread]
        public static int Main(string[] args)
        {
            var app = new App();
            app.InitializeComponent();
            return app.Run();
        }
    }
}
#endif
