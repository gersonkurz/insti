using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using com.tikumo.regis3;
using Microsoft.Win32;
using System.Xml;
using System.IO;
using System.IO.Compression;
using System.Security.AccessControl;
using System.Security.Principal;

namespace insticore
{
    public class ProjectItemRegistry : IProjectItem
    {
        private readonly string KeyName;
        private readonly string NameInArchive;

        public IProjectItem Clone()
        {
            return new ProjectItemRegistry(KeyName, NameInArchive);
        }

        public ProjectItemRegistry(string name, string archive)
        {
            KeyName = name;
            NameInArchive = archive;
        }

        public bool Uninstall()
        {
            Regis3.DeleteKeyRecursive(KeyName, RegistryView.Registry32);
            return true;
        }

        public void WriteInstallationXml(XmlWriter writer)
        {
            writer.WriteStartElement("registry");
            writer.WriteAttributeString("key", KeyName);
            writer.WriteAttributeString("archive", NameInArchive);
            writer.WriteEndElement();
        }

        public bool Exists()
        {
            string rootPathWithoutHive;
            using (RegistryKey rootKey = Regis3.OpenRegistryHive(KeyName, out rootPathWithoutHive, RegistryView.Registry32))
            {
                RegKeyEntry entry = new RegistryImporter(rootKey, rootPathWithoutHive).Import();
                return (entry != null);
            }
        }


        public bool Backup(ZipArchive archive)
        {
            string rootPathWithoutHive;
            using (RegistryKey rootKey = Regis3.OpenRegistryHive(KeyName, out rootPathWithoutHive, RegistryView.Registry32))
            {
                RegKeyEntry entry = new RegistryImporter(rootKey, rootPathWithoutHive).Import();
                string tempFileName = Path.GetTempFileName();
                new RegFileFormat4Exporter().Export(entry, tempFileName, RegFileExportOptions.NoEmptyKeys);

                archive.CreateEntryFromFile(tempFileName, NameInArchive, CompressionLevel.Optimal);

                File.Delete(tempFileName);
            }
            return true;
        }


        public bool Restore(ZipArchive archive)
        {
            int n = NameInArchive.Length;
            bool success = true;
            foreach (var entry in archive.Entries)
            {
                if (entry.FullName.Equals(NameInArchive))
                {
                    string tempFileName = Path.GetTempFileName();
                    entry.ExtractToFile(tempFileName, true);

                    RegKeyEntry key = RegFile.CreateImporterFromFile(tempFileName, RegFileImportOptions.None).Import();
                    key.WriteToTheRegistry(RegistryWriteOptions.AllAccessForEveryone | RegistryWriteOptions.Recursive, null, RegistryView.Registry32);
                    File.Delete(tempFileName);
               }
            }
            return success;
        }
    }
}
