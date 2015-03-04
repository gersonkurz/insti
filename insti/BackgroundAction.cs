using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.ComponentModel;
using insticore;

namespace insti
{
    public abstract class BackgroundAction : IReportProgress
    {
        public delegate void SetOutputTextDelegate(string message);
        protected SetOutputTextDelegate SetOutputText;
        protected BackgroundWorker Worker;

        public BackgroundAction()
        {
        }

        public void Bind(BackgroundWorker worker)
        {
            Worker = worker;
        }

        public void Setup(SetOutputTextDelegate setOutputText)
        {
            SetOutputText = setOutputText;
        }

        public abstract void DoWork();

        public void ShowText(string message)
        {
            SetOutputText(message);
        }
    }
}
