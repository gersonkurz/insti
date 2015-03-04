using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using log4net;
using System.Reflection;
using System.Collections.ObjectModel;
using MahApps.Metro.Controls;
using System.ComponentModel;
using System.IO;
using MahApps.Metro.Controls.Dialogs;

namespace insti
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : MetroWindow
    {
        private static readonly ILog Log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);
        public static ObservableCollection<InstallationItem> Items = new ObservableCollection<InstallationItem>();
        private readonly BackgroundWorker worker = new BackgroundWorker();
        public static string BaseDirectory;
        public static string InstallationFile;
        private insticore.ProjectDescription CurrentProjectDescription;

        public MainWindow()
        {
            InitializeComponent();

            var s = new insti.Properties.Settings();
            BaseDirectory = Environment.ExpandEnvironmentVariables(s.BaseDirectory);
            InstallationFile = Environment.ExpandEnvironmentVariables(s.InstallationFile);
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            MainItemsControl.ItemsSource = Items;
            worker.DoWork += worker_DoWork;
            worker.RunWorkerCompleted += worker_RunWorkerCompleted;
            worker.RunWorkerAsync();
        }

        private void worker_DoWork(object sender, DoWorkEventArgs e)
        {
            CurrentProjectDescription = insticore.ProjectDescription.FromFile(InstallationFile);
            if (CurrentProjectDescription == null)
            {
                CurrentProjectDescription = insticore.ProjectDescription.FromDefault(BaseDirectory, "UNKNOWN", "UNKNOWN");
                if (CurrentProjectDescription != null)
                {
                    if( CurrentProjectDescription.Exists() )
                    {
                        Dispatcher.Invoke(new Action(() => Items.Add(new InstallationItem(CurrentProjectDescription, true))));
                    }
                    else
                    {
                        CurrentProjectDescription = null;
                    }
                }
            }

            foreach (string file in Directory.GetFiles(BaseDirectory))
            {
                if(file.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                {
                    try
                    {
                        var desc = insticore.ProjectDescription.FromArchive(file);
                        if( desc != null )
                        {
                            bool isDefault = false;
                            if (CurrentProjectDescription != null)
                            {
                                isDefault = CurrentProjectDescription.Archive.Equals(desc.Archive, StringComparison.OrdinalIgnoreCase);
                            }

                            Dispatcher.Invoke(new Action(() => Items.Add( new InstallationItem(desc, isDefault))));
                        }
                    }
                    catch(Exception ex)
                    {
                        Console.WriteLine(ex.ToString());
                    }
                }
            }
            
        }

        private async void worker_RunWorkerCompleted(object sender,
                                               RunWorkerCompletedEventArgs e)
        {
            if( CurrentProjectDescription == null )
            {
                await this.ShowMessageAsync("Error", "Broken insti-installation: you have no installation, and no default description file: aborting");
                Close();
            }

            if( Items.Count == 0 )
            {
                
                BtSnapshot.IsEnabled = false;
                BtCloneAs.IsEnabled = false;
                BtRevert.IsEnabled = false;

                await this.ShowMessageAsync("Warning", "You have no existing backup copies, and we cannot see any existing installation...");
            }
            else
            {
                if (CurrentProjectDescription.Description.Equals("UNKNOWN"))
                {
                    if( Items.Count == 1 )
                    {
                        await this.ShowMessageAsync("Warning", "You have no existing backup copies. We recommend you start by creating a backup of your current installation...");
                    }
                }
            }
        }

        private void ProjectHasBeenRenamed()
        {
            foreach(InstallationItem item in Items)
            {
                if(item.ProjectDescription == CurrentProjectDescription)
                {
                    item.NotifyPropertyChanged("Name");
                    item.NotifyPropertyChanged("Description");
                    if (!item.IsCurrent)
                    {
                        item.IsCurrent = true;
                        item.NotifyPropertyChanged("IsCurrent");
                    }
                }
                else if(item.IsCurrent)
                {
                    item.IsCurrent = false;
                    item.NotifyPropertyChanged("IsCurrent");
                }
            }
        }

        private async void BtBackup_Click(object sender, RoutedEventArgs e)
        {
            if( !CurrentProjectDescription.IsValid )
            {
                var dialog = new BackupSettings(CurrentProjectDescription);
                dialog.Owner = this;
                bool? result = dialog.ShowDialog();
                if (!result.HasValue || !result.Value)
                    return;
            }

            ProjectHasBeenRenamed();

            DateTime start = DateTime.Now;
            var backupDialog = new LongRunningFunctionWindow(new BackupCurrentInstallation(CurrentProjectDescription), "BACKUP IN PROGRESS");
            backupDialog.Owner = this;
            backupDialog.ShowDialog();
            await this.ShowMessageAsync(CurrentProjectDescription.ShortName, string.Format("Backup complete after {0}", DateTime.Now - start));

        }
    }
}
