using System.Linq;
using System.Windows;
using System.Windows.Controls;
using EniBox.GUI.Models;
using EniBox.GUI.ViewModels;

namespace EniBox.GUI.Views
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        private void FileDataGrid_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (DataContext is MainViewModel vm && sender is DataGrid grid)
            {
                vm.SelectedFileItems.Clear();
                foreach (var item in grid.SelectedItems.Cast<PackFileItem>())
                    vm.SelectedFileItems.Add(item);
            }
        }

        private void Window_Drop(object sender, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop) && DataContext is MainViewModel vm)
            {
                var files = (string[])e.Data.GetData(DataFormats.FileDrop);
                vm.AddDroppedFiles(files);
            }
        }

        private void Window_DragOver(object sender, DragEventArgs e)
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
            e.Handled = true;
        }
    }
}
