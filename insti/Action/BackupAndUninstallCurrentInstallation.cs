using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using insticore;

namespace insti.Action
{
    class BackupThenUninstall : BackgroundAction
    {
        private readonly ProjectDescription Description;

        public BackupThenUninstall(ProjectDescription description)
        {
            Description = description;
        }

        public override void DoWork()
        {
            Description.Backup(MainWindow.BaseDirectory, false, 0, this);
            Description.Uninstall();
        }
    }
}
