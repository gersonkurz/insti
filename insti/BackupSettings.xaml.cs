using MahApps.Metro.Controls;
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
using System.Windows.Shapes;
using insticore;
using System.Diagnostics;

namespace insti
{
    /// <summary>
    /// Interaction logic for BackupSettings.xaml
    /// </summary>
    public partial class BackupSettings : MetroWindow
    {
        private readonly insticore.ProjectDescription Project;

        public BackupSettings(insticore.ProjectDescription project)
        {
            InitializeComponent();
            Project = project;
        }


        private void OnOK(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
            Trace.Assert(HasValidEntries());
            Project.ShortName = TbName.Text;
            Project.Description = TbDescription.Text;
            Close();
        }

        private void OnCancel(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void MetroWindow_Loaded(object sender, RoutedEventArgs e)
        {
            if( Project.IsValid )
            {
                TbDescription.Text = Project.Description;
                TbName.Text = Project.ShortName;
            }
            OKButton.IsEnabled = HasValidEntries();
            Keyboard.Focus(TbName);
        }

        private bool HasValidEntries()
        {
            LbError.Content = "";
            if (string.IsNullOrEmpty(TbDescription.Text) )
            {
                LbError.Content = "DESCRIPTION must not be empty";
                return false;
            }
            if (string.IsNullOrEmpty(TbName.Text) )
            {
                LbError.Content = "NAME must not be empty";
                return false;
            }
            if( TbName.Text.Equals("UNKNOWN", StringComparison.OrdinalIgnoreCase))
            {
                LbError.Content = "NAME must not be UNKNOWN";
                return false;
            }
            return true;
        }

        private void TbName_TextChanged(object sender, TextChangedEventArgs e)
        {
            OKButton.IsEnabled = HasValidEntries();
        }
    }
}
