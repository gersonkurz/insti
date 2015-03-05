using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace insticore
{
    public interface IProjectItemRunInfo
    {
        void Run();
        void WriteInstallationXml(XmlWriter writer);
        IProjectItemRunInfo Clone();
    }
}
