using System;
using System.Collections.Generic;
using System.IO;
using log4net;
using System.Reflection;
using System.Threading.Tasks;
using insticore;

namespace insti2.LongRunningFunction
{
    class ReadExistingInstallations
    {
        private static readonly ILog Log = LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);
        private ProjectDescription CurrentProjectDescription;
        private readonly string BaseDirectory;
        private readonly string InstallationFile;
        private readonly List<ProjectDescription> Projects = new List<ProjectDescription>();

        public ReadExistingInstallations()
        {
            var s = new insti2.Properties.Settings();
            BaseDirectory = Environment.ExpandEnvironmentVariables(s.BaseDirectory);
            InstallationFile = Environment.ExpandEnvironmentVariables(s.InstallationFile);
        }

        public void Run()
        {
            CurrentProjectDescription = ProjectDescription.FromFile(InstallationFile);
            if (CurrentProjectDescription == null)
            {
                CurrentProjectDescription = ProjectDescription.FromDefault(BaseDirectory,
                    string.Format("Installation at '{0}'", Path.GetDirectoryName(InstallationFile)), "UNKNOWN");
                if (CurrentProjectDescription != null)
                {
                    if (CurrentProjectDescription.Exists())
                    {
                        Projects.Add(CurrentProjectDescription);
                    }
                    else
                    {
                        CurrentProjectDescription = null;
                    }
                }
            }

            foreach (string file in Directory.GetFiles(BaseDirectory))
            {
                if (file.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                {
                    try
                    {
                        var desc = ProjectDescription.FromArchive(file);
                        if (desc != null)
                        {
                            bool isDefault = false;
                            if (CurrentProjectDescription != null)
                            {
                                isDefault = CurrentProjectDescription.Archive.Equals(desc.Archive, StringComparison.OrdinalIgnoreCase);
                            }
                            Projects.Add(desc);
                        }
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine(ex.ToString());
                    }
                }
            }
        }
    }
}
