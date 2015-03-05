using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using insticore;

namespace insti.Action
{
    class Backup : BackgroundAction
    {
        private readonly ProjectDescription Description;

        public Backup(ProjectDescription description)
        {
            Description = description;
        }

        public override void DoWork()
        {
            Description.Backup(MainWindow.BaseDirectory, false, 0, this);
        }
    }
}
