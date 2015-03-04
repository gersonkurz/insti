﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using System.Data;
using System.Diagnostics;
using System.IO.Compression;

namespace insticore
{
    public class ProjectDescription
    {

        private enum RecordingMode
        {
            Options,
            Startup,
            Shutdown
        };

        public string Name;
        public string Archive;
        public readonly List<IProjectItem> Items = new List<IProjectItem>();
        public readonly List<IProjectItemRunInfo> ShutdownItems = new List<IProjectItemRunInfo>();
        public readonly List<IProjectItemRunInfo> StartupItems = new List<IProjectItemRunInfo>();

        private ProjectDescription(string name, string archive)
        {
            Name = name;
            Archive = archive;
        }

        public override string ToString()
        {
            return string.Format("Project '{0}' ({1})", Name, Archive);
        }

        public static ProjectDescription FromDefault(string baseDirectory, string name, string archive)
        {
            ProjectDescription result = FromFile(Path.Combine(baseDirectory, "installation.xml"));
            if(result != null)
            {
                result.Name = name;
                result.Archive = archive;
            }
            return result;
        }

        public static ProjectDescription FromFile(string xmlFilename)
        {
            if (!File.Exists(xmlFilename))
                return null;

            ProjectDescription result = null;

            using (StreamReader sr = new StreamReader(xmlFilename))
            {
                using (XmlReader reader = XmlReader.Create(sr))
                {
                    RecordingMode rm = RecordingMode.Options;
                    List<IProjectItemRunInfo> activeItems = null;

                    while (reader.Read())
                    {
                        switch (reader.NodeType)
                        {
                            case XmlNodeType.Element:
                                if (rm == RecordingMode.Options)
                                {
                                    if (reader.Name.Equals("installation"))
                                    {
                                        result = new ProjectDescription(
                                            reader.GetAttribute("name"),
                                            reader.GetAttribute("archive"));
                                    }
                                    else if (reader.Name.Equals("files"))
                                    {
                                        result.Items.Add(new ProjectItemFiles(
                                            reader.GetAttribute("folder"),
                                            reader.GetAttribute("archive")));
                                    }
                                    else if (reader.Name.Equals("registry"))
                                    {
                                        result.Items.Add(new ProjectItemRegistry(
                                            reader.GetAttribute("key"),
                                            reader.GetAttribute("archive")));
                                    }
                                    else if (reader.Name.Equals("tcpip-service"))
                                    {
                                        result.Items.Add(new ProjectItemTcpipService(
                                            reader.GetAttribute("name"),
                                            reader.GetAttribute("port")));
                                    }
                                    else if (reader.Name.Equals("shutdown"))
                                    {
                                        rm = RecordingMode.Shutdown;
                                        activeItems = result.ShutdownItems;
                                    }
                                    else if (reader.Name.Equals("startup"))
                                    {
                                        rm = RecordingMode.Startup;
                                        activeItems = result.StartupItems;
                                    }
                                    else
                                    {
                                        Console.WriteLine("ERROR READING {0}: '{1}' is not a supported tag", xmlFilename, reader.Name);
                                    }
                                }
                                else if ((rm == RecordingMode.Shutdown) || (rm == RecordingMode.Startup))
                                {
                                    if (reader.Name.Equals("run-sync"))
                                    {
                                        activeItems.Add(new ProjectItemRunSync(reader.GetAttribute("file")));
                                    }
                                    else if (reader.Name.Equals("kill"))
                                    {
                                        activeItems.Add(new ProjectItemKillProcess(reader.GetAttribute("process-name")));
                                    }
                                    else
                                    {
                                        Console.WriteLine("ERROR READING {0}: '{1}' is not a supported tag", xmlFilename, reader.Name);
                                    }
                                }
                                break;
                            case XmlNodeType.Text:
                                break;
                            case XmlNodeType.EndElement:
                                if (reader.Name.Equals("shutdown"))
                                {
                                    rm = RecordingMode.Options;
                                }
                                else if (reader.Name.Equals("startup"))
                                {
                                    rm = RecordingMode.Options;
                                }
                                break;

                        }
                    }
                }
            }
            return result;
        }

        public static ProjectDescription FromArchive(string archiveName)
        {
            using (ZipArchive archive = ZipFile.OpenRead(archiveName))
            {
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    if( entry.FullName == "installation.xml")
                    {
                        string tempFileName = Path.GetTempFileName();
                        entry.ExtractToFile(tempFileName, true);

                        ProjectDescription result = FromFile(tempFileName);
                        File.Delete(tempFileName);
                        return result;
                    }
                }
            }
            // todo: show warning that this is a broken file format
            return null;
        }

        public void Shutdown()
        {
            foreach(IProjectItemRunInfo item in ShutdownItems)
            {
                item.Run();
            }
        }

        private void WriteInstallationXml(XmlWriter writer)
        {
            writer.WriteStartElement("installation");
            writer.WriteAttributeString("name", Name);
            writer.WriteAttributeString("archive", Archive);

            foreach (var item in Items)
            {
                item.WriteInstallationXml(writer);
            }

            writer.WriteStartElement("startup");
            foreach (var item in StartupItems)
            {
                item.WriteInstallationXml(writer);
            }
            writer.WriteEndElement();

            writer.WriteStartElement("shutdown");
            foreach (var item in ShutdownItems)
            {
                item.WriteInstallationXml(writer);
            }
            writer.WriteEndElement();
            writer.WriteEndElement(); // installation
        }

        private string CreateInstallationXml()
        {
            XmlWriterSettings settings = new XmlWriterSettings();
            settings.Indent = true;
            StringBuilder builder = new StringBuilder();

            using (XmlWriter writer = XmlWriter.Create(builder, settings))
            {
                writer.WriteStartDocument();
                WriteInstallationXml(writer);
                writer.WriteEndDocument();
            }
            return builder.ToString();
        }

        private void RenameExistingSnapshots(string baseDirectory, int maxSnapshots)
        {
            // find existing snapshots and rename them

            if( maxSnapshots <= 0 )
                maxSnapshots = 999;

            QuietDelete(NamedSnapshot(baseDirectory, maxSnapshots));

            for( int i = maxSnapshots-1; i > 0; --i )
            {
                QuietRename(NamedSnapshot(baseDirectory, i), NamedSnapshot(baseDirectory, i + 1));
            }
        }

        private string NamedSnapshot(string baseDirectory, int n)
        {
            string filename = Path.Combine(baseDirectory, Archive);

            if (filename.EndsWith(".zip", StringComparison.CurrentCultureIgnoreCase))
            {
                filename = filename.Substring(0, filename.Length - 4);
            }
            filename = string.Format("{0}@{1}", filename, n);
            if (!filename.EndsWith(".zip", StringComparison.CurrentCultureIgnoreCase))
            {
                filename = filename + ".zip";
            }
            return filename;
        }

        private void QuietRename(string a, string b)
        {
            
            try
            {
                if (File.Exists(a))
                {
                    Console.WriteLine("{0} => {1}", a, b);
                    File.Move(a, b);
                }
            }
            catch (Exception)
            {
            }
        }

        private void QuietDelete(string a)
        {
            try
            {
                if( File.Exists(a) )
                {
                    Console.WriteLine("Remove {0}", a);
                    File.SetAttributes(a, FileAttributes.Normal);
                    File.Delete(a);
                }
            }
            catch(Exception)
            {
            }
        }
        public bool IsValidSnapshotIndex(string baseDirectory, int nRevertIndex, out string snapshotFilename)
        {
            snapshotFilename = NamedSnapshot(baseDirectory, nRevertIndex);
            return File.Exists(snapshotFilename);
        }

        private void Report(IReportProgress progress, string msg)
        {
            progress.ShowText(msg);
        }
        private void Report(IReportProgress progress, string msg, params object[] args)
        {
            progress.ShowText(string.Format(msg, args));
        }

        public bool Backup(string baseDirectory, bool snapshot, int maxSnapshots, IReportProgress progress)
        {
            Shutdown();

            string filename = Path.Combine(baseDirectory, Archive);

            if(snapshot)
            {
                RenameExistingSnapshots(baseDirectory, maxSnapshots);

                if (filename.EndsWith(".zip", StringComparison.CurrentCultureIgnoreCase))
                {
                    filename = filename.Substring(0, filename.Length - 4);
                }
                filename = filename + "@0"; // snapshot 0
            }

            if( !filename.EndsWith(".zip", StringComparison.CurrentCultureIgnoreCase))
            {
                filename = filename + ".zip";
            }

            Report(progress, "Creating '{0}'", filename);
            using (var archive = new ProjectArchive(filename))
            {
                archive.AddString(CreateInstallationXml(), "installation.xml");
                foreach (IProjectItem item in Items)
                {
                    Report(progress, item.ToString());
                    item.Backup(archive.Archive);
                }
                Report(progress, "Compressing...");
            }
            Report(progress, "Done.");
            return true;
        }

        public bool Uninstall()
        {
            Shutdown();
            bool success = true;
            foreach (IProjectItem item in Items)
            {
                if( !item.Uninstall() )
                {
                    success = false;
                }
            }
            return success;
        }

        public bool Restore(string baseDirectory, string configurationFile)
        {
            string archiveName = Path.Combine(baseDirectory, Archive);
            if (!archiveName.EndsWith(".zip", StringComparison.CurrentCultureIgnoreCase))
            {
                archiveName = archiveName + ".zip";
            }
            bool success = true;
            using (ZipArchive archive = ZipFile.OpenRead(archiveName))
            {
                foreach (IProjectItem item in Items)
                {
                    if (!item.Restore(archive))
                    {
                        success = false;
                    }
                }
            }

            File.WriteAllText(configurationFile, CreateInstallationXml());
            return success;
        }
    }
}
