using System;
using System.IO;

namespace EniBox.GUI.Services
{
    /// <summary>
    /// 未处理异常的文件日志兜底：追加写 %LOCALAPPDATA%\EniBox\logs\。
    /// 兜底日志自身绝不能抛异常——写失败时静默放弃，否则兜底会变成新的崩溃源。
    /// </summary>
    public static class ErrorLog
    {
        public static string LogDirectory => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "EniBox", "logs");

        public static void Write(string source, Exception? ex)
            => Write(source, ex?.ToString() ?? "(null exception)");

        public static void Write(string source, string message)
        {
            try
            {
                Directory.CreateDirectory(LogDirectory);
                var line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{source}] {message}{Environment.NewLine}";
                File.AppendAllText(Path.Combine(LogDirectory, $"enibox-{DateTime.Now:yyyy-MM-dd}.log"), line);
            }
            catch
            {
                // 日志自身失败时静默放弃
            }
        }
    }
}
