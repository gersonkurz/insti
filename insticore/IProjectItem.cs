using System;
using System.Collections.Generic;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace insticore
{
    public interface IProjectItem
    {
        bool Uninstall();
        bool Backup(ZipArchive archive);
        bool Restore(ZipArchive archive);

        void WriteInstallationXml(XmlWriter writer);
        
    }
}
