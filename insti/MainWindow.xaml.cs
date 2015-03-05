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
            // snapshot & revert are not supported yet
            BtSnapshot.IsEnabled = false;
            BtRevert.IsEnabled = false;

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
                CurrentProjectDescription = insticore.ProjectDescription.FromDefault(BaseDirectory, 
                    string.Format("Installation at '{0}'", System.IO.Path.GetDirectoryName(InstallationFile)), "UNKNOWN");
                if (CurrentProjectDescription != null)
                {
                    if( CurrentProjectDescription.Exists() )
                    {
                        Dispatcher.Invoke(new System.Action(() => Items.Add(new InstallationItem(CurrentProjectDescription, true))));
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

                            Dispatcher.Invoke(new System.Action(() => Items.Add( new InstallationItem(desc, isDefault))));
                        }
                    }
                    catch(Exception ex)
                    {
                        Console.WriteLine(ex.ToString());
                    }
                }
            }
        }

        private async void worker_RunWorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            if( CurrentProjectDescription == null )
            {
                if (Items.Count == 0)
                {
                    await this.ShowMessageAsync("Error", "Broken insti-installation: you have no installation, and no default description file: aborting");
                    Close();
                }
                else
                {
                    HasNoCurrentInstallation();
                }
            }
            else if( Items.Count == 0 )
            {
                HasUnknownInstallation();

                await this.ShowMessageAsync("Warning", "You have no existing backup copies, and we cannot see any existing installation...");
            }
            else
            {
                HasCurrentInstallation();
                if (!CurrentProjectDescription.IsValid)
                {
                    if( Items.Count == 1 )
                    {
                        HasUnknownInstallation();
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
                        item.NotifyPropertyChanged("BackgroundBrush");
                    }
                }
                else if(item.IsCurrent)
                {
                    item.IsCurrent = false;
                    item.NotifyPropertyChanged("BackgroundBrush");
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

                CurrentProjectDescription.UpdateLocalInstallationXml(InstallationFile);
            }

            ProjectHasBeenRenamed();

            DateTime start = DateTime.Now;
            var backupDialog = new LongRunningFunctionWindow(new Action.Backup(CurrentProjectDescription), "BACKUP IN PROGRESS");
            backupDialog.Owner = this;
            backupDialog.ShowDialog();
            await this.ShowMessageAsync(CurrentProjectDescription.ShortName, string.Format("Backup complete after {0}", DateTime.Now - start));
        }

        /// <summary>
        /// no current installation: no buttons
        /// </summary>
        private void HasNoCurrentInstallation()
        {
            BtCloneAs.IsEnabled = false;
            BtBackup.IsEnabled = false;
            BtUninstall.IsEnabled = false;
        }

        /// <summary>
        /// has a current installation previously done with insti: all options are open :)
        /// </summary>
        private void HasCurrentInstallation()
        {
            BtCloneAs.IsEnabled = true;
            BtBackup.IsEnabled = true;
            BtUninstall.IsEnabled = true;
        }

        /// <summary>
        /// has an installation, but it is not known yet: you can do backup and uninstall, but not clone
        /// </summary>
        private void HasUnknownInstallation()
        {
            BtCloneAs.IsEnabled = false;
            BtBackup.IsEnabled = true;
            BtUninstall.IsEnabled = true;
        }

        private void SetCurrentInstallation(insticore.ProjectDescription description)
        {
            CurrentProjectDescription = description;
            if (description == null)
            {
                HasNoCurrentInstallation();
            }
            else
            {
                HasCurrentInstallation();
            }
        }

        private async void BtUninstall_Click(object sender, RoutedEventArgs e)
        {
            var settings = new MetroDialogSettings();
            settings.AffirmativeButtonText = "UNINSTALL ONLY";
            settings.FirstAuxiliaryButtonText = "CANCEL";
            settings.NegativeButtonText = "BACKUP, THEN UNINSTALL";

            var result = await this.ShowMessageAsync("UNINSTALL",
                string.Format("Are you sure you want to uninstall {0}?", CurrentProjectDescription.ShortName),
                MessageDialogStyle.AffirmativeAndNegativeAndSingleAuxiliary,
                settings);

            if (result == MessageDialogResult.Affirmative)
            {
                // ok, uninstall the software
                DateTime start = DateTime.Now;
                var backupDialog = new LongRunningFunctionWindow(new Action.Uninstall(CurrentProjectDescription), "UNINSTALL IN PROGRESS");
                backupDialog.Owner = this;
                backupDialog.ShowDialog();
                await this.ShowMessageAsync(CurrentProjectDescription.ShortName, string.Format("Uninstalled after {0}", DateTime.Now - start));
                SetCurrentInstallation(null);
                ProjectHasBeenRenamed();
            }
            else if( result == MessageDialogResult.Negative)
            {
                // backup, then uninstall 
                // ok, uninstall the software
                DateTime start = DateTime.Now;
                var backupDialog = new LongRunningFunctionWindow(new Action.BackupThenUninstall(CurrentProjectDescription), "BACKUP & UNINSTALL IN PROGRESS");
                backupDialog.Owner = this;
                backupDialog.ShowDialog();
                await this.ShowMessageAsync(CurrentProjectDescription.ShortName, string.Format("Uninstalled after {0}", DateTime.Now - start));
                SetCurrentInstallation(null);
                ProjectHasBeenRenamed();
            }
            else
            {
                // chicken out
            }

            
        }

        private async void Tile_Click(object sender, RoutedEventArgs e)
        {
            Tile tile = sender as Tile;
            if( tile != null )
            {
                if( CurrentProjectDescription != null )
                {
                    // ask if the user wants to backup existing installation first
                }

                var iitem = tile.DataContext as InstallationItem;

                SetCurrentInstallation(iitem.ProjectDescription);
                DateTime start = DateTime.Now;
                var dialog = new LongRunningFunctionWindow(new Action.RestoreFromArchive(CurrentProjectDescription), "SWITCH TO " + iitem.Name);
                dialog.Owner = this;
                dialog.ShowDialog();
                
                await this.ShowMessageAsync(CurrentProjectDescription.ShortName, string.Format("Restored after {0}", DateTime.Now - start));
                ProjectHasBeenRenamed();
                
            }
        }

        private async void BtCloneAs_Click(object sender, RoutedEventArgs e)
        {
            if( CurrentProjectDescription != null )
            {
                insticore.ProjectDescription newDescription = CurrentProjectDescription.Clone();

                var dialog = new BackupSettings(newDescription, true);
                dialog.Owner = this;
                bool? result = dialog.ShowDialog();
                if (!result.HasValue || !result.Value)
                    return;

                Items.Add(new InstallationItem(newDescription, true));
                newDescription.UpdateLocalInstallationXml(InstallationFile);
                SetCurrentInstallation(newDescription);
                ProjectHasBeenRenamed();

                DateTime start = DateTime.Now;
                var backupDialog = new LongRunningFunctionWindow(new Action.Backup(CurrentProjectDescription), "CLONE IN PROGRESS");
                backupDialog.Owner = this;
                backupDialog.ShowDialog();
                await this.ShowMessageAsync(CurrentProjectDescription.ShortName, string.Format("Clone complete after {0}", DateTime.Now - start));
            }
        }
    }
}
