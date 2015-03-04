using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using System.IO;
using System.IO.Compression;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text.RegularExpressions;

namespace insticore
{
    class ProjectItemFiles : IProjectItem
    {
        private readonly string Name;
        private readonly string Archive;
        private readonly string ExpandedName;
        private readonly HashSet<string> CreatedDirectories = new HashSet<string>();

        public ProjectItemFiles(string name, string archive)
        {
            Name = name;
            Archive = archive;
            if (name.Contains('%'))
            {
                ExpandedName = Environment.ExpandEnvironmentVariables(name);
            }
            else
            {
                ExpandedName = name;
            }
        }

        public bool Uninstall()
        {
            return DeleteFilesRecursive(ExpandedName);
        }

        public bool Exists()
        {
            return Directory.Exists(ExpandedName);
        }

        private bool DeleteFilesRecursive(string folder)
        {
            var dir = new DirectoryInfo(folder);
            bool success = true;
            foreach (var fi in dir.GetFiles())
            {
                try
                {
                    File.SetAttributes(fi.FullName, FileAttributes.Normal);
                    fi.Delete();
                }
                catch(Exception)
                {
                    success = false;
                }
            }

            foreach (var di in dir.GetDirectories())
            {
                if( !DeleteFilesRecursive(di.FullName) )
                {
                    success = false;
                }
            }

            try
            {
                File.SetAttributes(folder, FileAttributes.Normal);
                Directory.Delete(folder);
            }
            catch (Exception)
            {
                success = false;
            }
            return success;
        }

        public void WriteInstallationXml(XmlWriter writer)
        {
            writer.WriteStartElement("files");
            writer.WriteAttributeString("folder", Name);
            writer.WriteAttributeString("archive", Archive);
            writer.WriteEndElement();
        }


        public bool Backup(ZipArchive archive)
        {
            return AddDirectory(archive, ExpandedName, Archive);
        }

        private bool AddDirectory(ZipArchive archive, string directory, string folderInArchive)
        {
            foreach (string filename in Directory.GetFiles(directory))
            {
                string filePart = Path.GetFileName(filename);

                if( filename.ToLower().Contains("jbos/persistence") &&
                    !filename.ToLower().Contains("jbos/persistence/logging.properties"))
                {
                    continue;
                }

                if (!Like(filePart, "*.log*") &&
                    !Like(filePart, "*.mem") &&
                    !filePart.Equals("installation.xml", StringComparison.OrdinalIgnoreCase))
                {
                    archive.CreateEntryFromFile(filename, Path.Combine(folderInArchive, Path.GetFileName(filename)), CompressionLevel.Optimal);
                }
            }

            int n = directory.Length;
            foreach (string subdirectory in Directory.GetDirectories(directory))
            {
                AddDirectory(archive, subdirectory, Path.Combine(folderInArchive, subdirectory.Substring(n + 1)));
            }
            return true;
        }

        private static bool Like(string str, string wildcard)
        {
            return new Regex(
                "^" + Regex.Escape(wildcard).Replace(@"\*", ".*").Replace(@"\?", ".") + "$",
                RegexOptions.IgnoreCase | RegexOptions.Singleline
            ).IsMatch(str);
        }



        public bool Restore(ZipArchive archive)
        {
            int n = Archive.Length;
            bool success = true;
            foreach(var entry in archive.Entries)
            {
                if(entry.FullName.StartsWith(Archive))
                {
                    if( !Restore(entry, n) )
                    {
                        success = false;
                    }
                }
            }
            return success;
        }

        private bool Restore(ZipArchiveEntry entry, int n)
        {
            bool success = true;
            string temp = entry.FullName.Substring(n + 1);
            string targetName = Path.Combine(ExpandedName, temp);

            string directory = Path.GetDirectoryName(targetName);
            string key = directory.ToLower();
            if( !CreatedDirectories.Contains(key) )
            {
                try
                {
                    DirectorySecurity sec = new DirectorySecurity();
                    SecurityIdentifier everyone = new SecurityIdentifier(WellKnownSidType.WorldSid, null);
                    sec.AddAccessRule(new FileSystemAccessRule(everyone, FileSystemRights.Modify | FileSystemRights.Synchronize, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));

                    Directory.CreateDirectory(directory, sec);
                    CreatedDirectories.Add(key);
                }
                catch(Exception e)
                {
                    Console.WriteLine(e);
                    success = false;
                }
            }
            entry.ExtractToFile(targetName, true);
            return success;
        }
    }
}
