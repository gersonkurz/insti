using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using insticore;

namespace insti.Action
{
    class Uninstall : BackgroundAction
    {
        private readonly ProjectDescription Description;

        public Uninstall(ProjectDescription description)
        {
            Description = description;
        }

        public override void DoWork()
        {
            Description.Uninstall();
        }
    }
}
