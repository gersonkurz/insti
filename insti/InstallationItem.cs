using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using log4net;
using System.Reflection;
using System.Windows.Media;

namespace insti
{
    public class InstallationItem : INotifyPropertyChanged
    {
        private static readonly ILog Log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);

        public event PropertyChangedEventHandler PropertyChanged;

        public string Name
        {
            get
            {
                return ProjectDescription.ShortName;
            }
        }
        public string Description
        {
            get
            {
                return ProjectDescription.Description;
            }
        }

        public bool IsCurrent;

        public Brush BackgroundBrush
        {
            get
            {
                return (Brush)(IsCurrent ? App.Current.FindResource("HighlightBrush") : App.Current.FindResource("AccentColorBrush2"));
            }
        }

        public override string ToString()
        {
            return ProjectDescription.Archive;
        }
        

        public readonly insticore.ProjectDescription ProjectDescription;

        public InstallationItem(insticore.ProjectDescription description, bool isCurrent)
        {
            ProjectDescription = description;
            IsCurrent = isCurrent;
        }

        public void NotifyPropertyChanged(string name)
        {
            if (PropertyChanged != null)
            {
                PropertyChanged(this, new PropertyChangedEventArgs(name));
            }
        }


        
    }
}
