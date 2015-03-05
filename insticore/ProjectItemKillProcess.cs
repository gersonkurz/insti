using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace insticore
{
    class ProjectItemKillProcess : IProjectItemRunInfo
    {
        public readonly string Name;


        public IProjectItemRunInfo Clone()
        {
            return new ProjectItemKillProcess(Name);
        }

        public ProjectItemKillProcess(string name)
        {
            Name = name;
        }

        public void Run()
        {
            foreach (var process in Process.GetProcessesByName(Name))
            {
                Console.WriteLine("Kill {0}", process);
                process.Kill();
            }
        }


        public void WriteInstallationXml(XmlWriter writer)
        {
            writer.WriteStartElement("kill");
            writer.WriteAttributeString("process-name", Name);
            writer.WriteEndElement();
        }

    }
}
