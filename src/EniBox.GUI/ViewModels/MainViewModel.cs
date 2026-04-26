using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using EniBox.GUI.Models;
using EniBox.GUI.Services;
using Microsoft.Win32;

namespace EniBox.GUI.ViewModels
{
    public partial class MainViewModel : ObservableObject
    {
        private readonly IPackService _packService;

        [ObservableProperty]
        private string _sourceExePath = string.Empty;

        [ObservableProperty]
        private string _outputPath = string.Empty;

        [ObservableProperty]
        private ObservableCollection<PackFileItem> _fileItems = new();

        private PackFileItem? _selectedFileItem;

        public PackFileItem? SelectedFileItem
        {
            get => _selectedFileItem;
            set => SetProperty(ref _selectedFileItem, value);
        }

        [ObservableProperty]
        private PackProgress? _packProgress;

        [ObservableProperty]
        private bool _isPacking;

        [ObservableProperty]
        private string _logText = string.Empty;

        [ObservableProperty]
        private bool _enableRegistryVirtualization;

        [ObservableProperty]
        private bool _enableSubProcessInjection = true;

        public MainViewModel()
        {
            var compressor = new LzmaCompressor();
            var vfsBuilder = new VfsBuilder(compressor);
            _packService = new PackService(compressor, vfsBuilder);
        }

        [RelayCommand]
        private void BrowseSource()
        {
            var dialog = new OpenFileDialog
            {
                Filter = "Executable Files (*.exe)|*.exe|All Files (*.*)|*.*",
                Title = "Select Source EXE"
            };

            if (dialog.ShowDialog() == true)
            {
                SourceExePath = dialog.FileName;
                if (string.IsNullOrEmpty(OutputPath))
                {
                    var dir = Path.GetDirectoryName(dialog.FileName) ?? "";
                    var name = Path.GetFileNameWithoutExtension(dialog.FileName);
                    OutputPath = Path.Combine(dir, $"{name}_packed.exe");
                }
            }
        }

        [RelayCommand]
        private void BrowseOutput()
        {
            var dialog = new SaveFileDialog
            {
                Filter = "Executable Files (*.exe)|*.exe|All Files (*.*)|*.*",
                Title = "Select Output Path",
                FileName = OutputPath
            };

            if (dialog.ShowDialog() == true)
            {
                OutputPath = dialog.FileName;
            }
        }

        [RelayCommand]
        private void AddFiles()
        {
            var dialog = new OpenFileDialog
            {
                Multiselect = true,
                Title = "Add Files"
            };

            if (dialog.ShowDialog() == true)
            {
                var baseDir = Path.GetDirectoryName(SourceExePath) ?? "";
                foreach (var file in dialog.FileNames)
                {
                    FileItems.Add(PackFileItem.FromFile(file, baseDir));
                }
            }
        }

        [RelayCommand]
        private void AddDirectory()
        {
            var dialog = new System.Windows.Forms.FolderBrowserDialog
            {
                Description = "Select Directory to Add"
            };

            if (dialog.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            {
                var baseDir = Path.GetDirectoryName(SourceExePath) ?? "";
                foreach (var file in Directory.EnumerateFiles(dialog.SelectedPath, "*", SearchOption.AllDirectories))
                {
                    FileItems.Add(PackFileItem.FromFile(file, baseDir));
                }
            }
        }

        [RelayCommand]
        private void RemoveFiles()
        {
            if (SelectedFileItem != null)
            {
                FileItems.Remove(SelectedFileItem);
                SelectedFileItem = null;
            }
        }

        [RelayCommand]
        private void SwitchLanguage(string language)
        {
            var culture = language switch
            {
                "zh-CN" => new System.Globalization.CultureInfo("zh-CN"),
                "en-US" => new System.Globalization.CultureInfo("en-US"),
                _ => System.Globalization.CultureInfo.CurrentUICulture
            };

            System.Threading.Thread.CurrentThread.CurrentUICulture = culture;
            // Refresh resources
            var app = Application.Current;
            app.Resources.MergedDictionaries.Clear();
            var dict = new ResourceDictionary
            {
                Source = new Uri($"Resources/Strings.{language}.xaml", UriKind.Relative)
            };
            app.Resources.MergedDictionaries.Add(dict);
        }

        [RelayCommand]
        private async Task PackAsync()
        {
            if (string.IsNullOrEmpty(SourceExePath))
            {
                MessageBox.Show("Please select a source EXE file first.");
                return;
            }

            IsPacking = true;
            LogText = string.Empty;

            var config = new PackConfiguration
            {
                SourceExePath = SourceExePath,
                OutputPath = OutputPath,
                Files = FileItems.ToList(),
                EnableRegistryVirtualization = EnableRegistryVirtualization,
                EnableSubProcessInjection = EnableSubProcessInjection
            };

            var progress = new Progress<PackProgress>(p =>
            {
                PackProgress = p;
            });

            try
            {
                var result = await _packService.PackAsync(config, progress, CancellationToken.None);

                if (result.IsSuccess)
                {
                    LogText += $"[SUCCESS] Output: {result.OutputPath} ({result.OutputFileSize} bytes)\n";
                    MessageBox.Show($"Packaging successful!\nOutput: {result.OutputPath}\nSize: {result.OutputFileSize} bytes",
                        "EniBox", MessageBoxButton.OK, MessageBoxImage.Information);
                }
                else
                {
                    LogText += $"[ERROR] {result.ErrorMessage}\n";
                    MessageBox.Show($"Packaging failed:\n{result.ErrorMessage}",
                        "EniBox", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
            catch (Exception ex)
            {
                LogText += $"[EXCEPTION] {ex.Message}\n";
                MessageBox.Show($"Unexpected error:\n{ex.Message}",
                    "EniBox", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                IsPacking = false;
            }
        }

        public void AddDroppedFiles(string[] paths)
        {
            var baseDir = Path.GetDirectoryName(SourceExePath) ?? "";
            foreach (var path in paths)
            {
                if (File.Exists(path))
                {
                    FileItems.Add(PackFileItem.FromFile(path, baseDir));
                }
                else if (Directory.Exists(path))
                {
                    foreach (var file in Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories))
                    {
                        FileItems.Add(PackFileItem.FromFile(file, baseDir));
                    }
                }
            }
        }
    }
}
