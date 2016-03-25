using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.IO.Compression;
using Microsoft.Win32;
using com.tikumo.regis3;
using System.Text.RegularExpressions;


namespace insticore
{
    public class ProjectArchive : IDisposable
    {
        private readonly FileStream ZipFile;
        public readonly ZipArchive Archive;


        public ProjectArchive(string filename)
        {
            ZipFile = new FileStream(filename, FileMode.Create);
            Archive = new ZipArchive(ZipFile, ZipArchiveMode.Update);
        }
        
        public bool AddString(string content, string filename)
        {
            ZipArchiveEntry readmeEntry = Archive.CreateEntry(filename);
            using (StreamWriter writer = new StreamWriter(readmeEntry.Open()))
            {
                writer.Write(content);
            }
            return true;
        }
        
        public void Dispose()
        {
            Archive.Dispose();
            ZipFile.Dispose();
        }
    }
}
