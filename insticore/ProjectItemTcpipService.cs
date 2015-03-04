using System;
using System.Collections.Generic;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace insticore
{
    class ProjectItemTcpipService : IProjectItem
    {
        public readonly string Name;
        public readonly string Port;

        public ProjectItemTcpipService(string name, string port)
        {
            Name = name;
            Port = port;
        }

        public void WriteInstallationXml(XmlWriter writer)
        {
            writer.WriteStartElement("tcpip-service");
            writer.WriteAttributeString("name", Name);
            writer.WriteAttributeString("port", Port);
            writer.WriteEndElement();
        }

        public bool Uninstall()
        {
            // not implemented yet 
            return true;
        }


        public bool Backup(ZipArchive archive)
        {
            // not implemented yet 
            return true;
        }

        public bool Restore(System.IO.Compression.ZipArchive archive)
        {
            // not implemented yet 
            return true;
        }


        public bool Exists()
        {
            return true;
        }
    }
}
