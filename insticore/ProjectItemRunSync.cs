using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace insticore
{
    class ProjectItemRunSync : IProjectItemRunInfo
    {
        private readonly string Name;
        private readonly string ExpandedName;

        public IProjectItemRunInfo Clone()
        {
            return new ProjectItemRunSync(Name);
        }


        public ProjectItemRunSync(string name)
        {
            Name = name;
            ExpandedName = Name;
            if (ExpandedName.Contains('%'))
            {
                ExpandedName = Environment.ExpandEnvironmentVariables(ExpandedName);
            }
        }

        public void Run()
        {
            try
            {
                ProcessStartInfo startInfo = new ProcessStartInfo(ExpandedName);
                Process process = Process.Start(startInfo);
                process.WaitForExit();
            }
            catch(Exception)
            {

            }
        }

        public void WriteInstallationXml(XmlWriter writer)
        {
            writer.WriteStartElement("run-sync");
            writer.WriteAttributeString("file", Name);
            writer.WriteEndElement();
        }
    }
}
