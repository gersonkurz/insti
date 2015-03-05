using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using insticore;

namespace insti.Action
{
    class RestoreFromArchive : BackgroundAction
    {
        private readonly ProjectDescription Description;

        public RestoreFromArchive(ProjectDescription description)
        {
            Description = description;
        }

        public override void DoWork()
        {
            Description.Restore(MainWindow.BaseDirectory, MainWindow.InstallationFile);
        }
    }
}
