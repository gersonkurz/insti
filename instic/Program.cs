using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using insticore;
using System.IO;
using System.Diagnostics;
using com.tikumo.regis3;
using Microsoft.Win32;

namespace instic
{
    class Program : IReportProgress
    {
        /*
         * - recognize if current installation has been modified
         * - revert to backup version
         * - create snapshot version
         * - switch to other version by first uninstalling, then installing new version
         */

        private string BaseDirectory;
        private string InstallationFile;
        private int MaxSnapshots;

        public Program()
        {
            var s = new instic.Properties.Settings();

            BaseDirectory = Environment.ExpandEnvironmentVariables(s.BaseDirectory);
            InstallationFile = Environment.ExpandEnvironmentVariables(s.InstallationFile);
            MaxSnapshots = s.MaxSnapshots;
        }

        private delegate bool HandlerMethod(ProjectDescription description, List<string> nextMethodArgs);

        public bool Run(string[] args)
        {
            Console.WriteLine("*** instic 1.0 - manage multiple ProAKT installations ***");
            Console.WriteLine();

            ProjectDescription description = ProjectDescription.FromFile(InstallationFile);
            if (description != null)
            {
                Console.WriteLine("Existing installation: {0}", description);
            }
            else
            {
                Console.WriteLine("Existing installation: - None -");
            }
            Console.WriteLine();

            HandlerMethod nextMethod = null;
            List<string> nextMethodArgs = new List<string>();

            foreach (string arg in args)
            {
                if (arg.Equals("/UNINSTALL", StringComparison.CurrentCultureIgnoreCase))
                {
                    nextMethod = Uninstall;
                }
                else if (arg.Equals("/BACKUP", StringComparison.CurrentCultureIgnoreCase))
                {
                    nextMethod = Backup;
                }
                else if (arg.Equals("/RESTORE", StringComparison.CurrentCultureIgnoreCase))
                {
                    nextMethod = Restore;
                }
                else if (arg.Equals("/SNAPSHOT", StringComparison.CurrentCultureIgnoreCase))
                {
                    nextMethod = Snapshot;
                }
                else if (arg.Equals("/REVERT", StringComparison.CurrentCultureIgnoreCase))
                {
                    nextMethod = Revert;
                }
                else if (arg.Equals("/?") || arg.Equals("/HELP", StringComparison.CurrentCultureIgnoreCase))
                {
                    return Help();
                }
                else if (nextMethod != null)
                {
                    nextMethodArgs.Add(arg);
                }
            }
            if( nextMethod != null )
            {
                return nextMethod(description, nextMethodArgs);
            }
            return ListInstallations(description);
        }

        private bool Backup(ProjectDescription description, List<string> args)
        {
            if( description == null )
            {
                // check to see if we can create a new backup from scratch
                if( args.Count == 1 )
                {
                    description = ProjectDescription.FromDefault(BaseDirectory, "", args[0]);
                }
                else if( args.Count == 2 )
                {
                    description = ProjectDescription.FromDefault(BaseDirectory, args[0], args[1]);
                }
                else
                {
                    Console.WriteLine("ERROR, cannot perform backup: installation.xml does not exist, and no archive name given.");
                    return false;
                }
                if( description == null )
                {
                    Console.WriteLine("ERROR, no default installation.xml exists in '{0}'", BaseDirectory);
                    return false;
                }
            }
            else
            {
                // maybe rename existing installation
                if (args.Count == 1)
                {
                    description.Archive = args[0];
                }
                else if (args.Count == 2)
                {
                    description.Description = args[0];
                    description.Archive = args[1];
                }
                else if( args.Count > 0)
                {
                    Console.WriteLine("ERROR, bad parameters for /BACKUP. Please consult your non-existing manual.");
                    return false;
                }
            }
                
            Trace.Assert(description != null);

            Console.WriteLine("Creating backup {0}", description);
            if( description.Backup(BaseDirectory, false, 0, this) )
            {
                Console.WriteLine("Done.");
                return true;
            }
            Console.WriteLine("Warning: unable to backup installation");
            return false;
        }

        private bool Snapshot(ProjectDescription description, List<string> args)
        {
            if (description == null)
            {
                Console.WriteLine("ERROR, no installation.xml exists: /SNAPSHOT is only possible for instic-installations");
                return false;
            }
            else if (args.Count != 0)
            {
                Console.WriteLine("ERROR, /SNAPSHOT has no arguments");
                return false;
            }

            Console.WriteLine("Creating snapshot {0}", description);
            if (description.Backup(BaseDirectory, true, MaxSnapshots, this))
            {
                Console.WriteLine("Done.");
                return true;
            }
            Console.WriteLine("Warning: unable to backup installation");
            return false;
        }

        private bool Uninstall(ProjectDescription description, List<string> args)
        {
            if (description != null)
            {
                Console.WriteLine("Removing {0}", description);
                if( description.Uninstall() )
                {
                    Console.WriteLine("Done.");
                    return true;
                }
                Console.WriteLine("Warning: unable to completely remove existing installation: please check manually");
                return false;
            }
            else
            {
                Console.WriteLine("Warning: no installation found: please check manually");
                return false;
            }
        }

        private bool Help()
        {
            Console.WriteLine("USAGE: instic [OPTIONS]");
            Console.WriteLine("OPTIONS:");
            Console.WriteLine("/BACKUP [name] .... create a backup with a new name");
            Console.WriteLine("/RESTORE <name> ... installed named version");
            Console.WriteLine("/UNINSTALL ........ uninstall existing installation");
            Console.WriteLine("/SNAPSHOT ......... create a new snapshot (max. NMAX snapshots)");
            Console.WriteLine("/REVERT [n] ....... revert to last-known-good (or snapshot)"); 
            return true;
        }

        private bool Restore(ProjectDescription description, List<string> args)
        {
            if( args.Count != 1 )
            {
                Console.WriteLine("Error: bad parameters for /RESTORE <name>");
                return false;
            }

            List<string> candidates = new List<string>();

            foreach (string file in Directory.GetFiles(BaseDirectory))
            {
                if (file.ToLower().Contains(args[0].ToLower()))
                {
                    candidates.Add(file);
                }
            }
            if (candidates.Count == 1)
            {
                return SwitchToThisOneInstallation(description, candidates[0]);
            }
            else if (candidates.Count > 1)
            {
                Console.WriteLine("Ambiguous specification. Possible matches are:");
                foreach (string candidate in candidates)
                {
                    Console.WriteLine(Path.GetFileName(candidate));
                }
            }
            else if (candidates.Count == 0)
            {
                Console.WriteLine("No match found. Possible candidates are:");
                ListInstallations(description);
            }
            return false;
        }

        private bool Revert(ProjectDescription description, List<string> args)
        {
            int nRevertIndex = 0;

            if (args.Count == 1)
            {
                nRevertIndex = int.Parse(args[0]);
            }
            else if(args.Count > 1)
            {
                Console.WriteLine("Error: bad parameters for /REVERT <name>");
                return false;
            }

            if (description == null)
            {
                Console.WriteLine("ERROR, no installation.xml exists: /REVERT is only possible for instic-installations");
                return false;
            }

            string snapshotFilename;
            if( !description.IsValidSnapshotIndex(BaseDirectory, nRevertIndex, out snapshotFilename))
            {
                Console.WriteLine("ERROR, {0} is not an existing snapshot index", nRevertIndex);
                return false;
            }

            Console.WriteLine("Removing existing installation");
            description.Uninstall();

            Console.WriteLine("Reverting back to snapshot #{0}", nRevertIndex);
            return ProjectDescription.FromArchive(snapshotFilename).Restore(BaseDirectory, InstallationFile);
        }

        private bool SwitchToThisOneInstallation(ProjectDescription description, string name, int nRevertIndex = -1)
        {
            if( description != null )
            {
                Console.WriteLine("Removing this: {0}", description);
                description.Uninstall();
            }
            Console.WriteLine("Restoring this: {0}", name);
            return ProjectDescription.FromArchive(name).Restore(BaseDirectory, InstallationFile);
        }

        private bool ListInstallations(ProjectDescription description)
        {

            foreach (string file in Directory.GetFiles(BaseDirectory))
            {
                Console.WriteLine(Path.GetFileName(file));
            }
            return false;
        }


        static void Main(string[] args)
        {
            new Program().Run(args);
        }

        public void ShowText(string message)
        {
            Console.WriteLine(message);
        }
    }
}
