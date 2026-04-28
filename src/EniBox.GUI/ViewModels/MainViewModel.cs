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

        /// <summary>
        /// Collection of selected items for multi-select removal support.
        /// Populated from DataGrid.SelectedItems via code-behind.
        /// </summary>
        public ObservableCollection<PackFileItem> SelectedFileItems { get; } = new();

        [ObservableProperty]
        private PackProgress? _packProgress;

        [ObservableProperty]
        private bool _isPacking;

        [ObservableProperty]
        private bool _canCancel;

        private CancellationTokenSource? _packCts;

        [ObservableProperty]
        private string _logText = string.Empty;

        [ObservableProperty]
        private bool _enableRegistryVirtualization;

        [ObservableProperty]
        private bool _enableSubProcessInjection = true;

        public MainViewModel(IPackService packService)
        {
            _packService = packService;
        }

        public MainViewModel() : this(
            new PackService(
                new LzmaCompressor(),
                new VfsBuilder(new LzmaCompressor())))
        {
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
            // Support multi-select removal via SelectedFileItems collection
            if (SelectedFileItems.Count > 0)
            {
                var itemsToRemove = SelectedFileItems.ToList();
                SelectedFileItems.Clear();
                foreach (var item in itemsToRemove)
                    FileItems.Remove(item);
                SelectedFileItem = null;
            }
            else if (SelectedFileItem != null)
            {
                // Fallback: single selection
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

            // Set both culture and UICulture for framework-level resource resolution
            System.Threading.Thread.CurrentThread.CurrentCulture = culture;
            System.Threading.Thread.CurrentThread.CurrentUICulture = culture;

            // Replace the resource dictionary (not clear+add, to avoid flicker)
            var app = Application.Current;
            var newDict = new ResourceDictionary
            {
                Source = new Uri($"Resources/Strings.{language}.xaml", UriKind.Relative)
            };

            // Find and replace the existing string resource dictionary
            var oldDict = app.Resources.MergedDictionaries
                .FirstOrDefault(d => d.Source?.OriginalString?.Contains("Strings.") == true);
            if (oldDict != null)
            {
                var index = app.Resources.MergedDictionaries.IndexOf(oldDict);
                app.Resources.MergedDictionaries.RemoveAt(index);
                app.Resources.MergedDictionaries.Insert(index, newDict);
            }
            else
            {
                app.Resources.MergedDictionaries.Add(newDict);
            }
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
            CanCancel = true;
            _packCts = new CancellationTokenSource();
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
                var result = await _packService.PackAsync(config, progress, _packCts.Token);

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
            catch (OperationCanceledException)
            {
                LogText += "[CANCELLED] Packaging was cancelled by user.\n";
                MessageBox.Show("Packaging cancelled.", "EniBox", MessageBoxButton.OK, MessageBoxImage.Warning);
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
                CanCancel = false;
                _packCts?.Dispose();
                _packCts = null;
            }
        }

        [RelayCommand(CanExecute = nameof(CanCancel))]
        private void CancelPack()
        {
            _packCts?.Cancel();
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
