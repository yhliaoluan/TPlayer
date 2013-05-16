using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace WinformPlayer
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
            this.Load += Form1_Load;
        }

        private IntPtr _handle = IntPtr.Zero;
        private FFSettings _settings = new FFSettings();
        private OnNewFrame _onNewFrame = null;
        private OnFinished _onFinished = null;
        private Stopwatch _stopwatch = new Stopwatch();
        private object _lock = new object();
        private Bitmap _bmp = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                if(components != null)
                    components.Dispose();
            }
            if (_handle != IntPtr.Zero)
                FFAPI.FF_CloseHandle(_handle);
            base.Dispose(disposing);
        }

        void Form1_Load(object sender, EventArgs e)
        {
            this.panelVideo.Paint += panelVideo_Paint;
            FFAPI.FF_Init();
            _onNewFrame = OnNewFrame;
            _onFinished = OnFinished;
        }

        void panelVideo_Paint(object sender, PaintEventArgs e)
        {
            if (_bmp == null)
                return;
            lock (_lock)
            {
                e.Graphics.DrawImageUnscaled((Bitmap)_bmp,
                    0, 0);
            }
        }

        void OnNewFrame(IntPtr frame)
        {
            FFFrame f = (FFFrame)Marshal.PtrToStructure(frame, typeof(FFFrame));
            try
            {
                int sleepMS = (int)(f.time * 1000 - _stopwatch.ElapsedMilliseconds);
                if (sleepMS > 0)
                    Thread.Sleep(sleepMS);

                //lock (_lock)
                //{
                //    PixelFormat pixfmt = PixelFormat.Format32bppPArgb;
                //    if (_bmp != null)
                //        _bmp.Dispose();
                //    _bmp = new Bitmap(f.width,
                //        f.height,
                //        pixfmt);
                //    BitmapData bmpData = new BitmapData();
                //    bmpData.Width = f.width;
                //    bmpData.Height = f.height;
                //    bmpData.Stride = f.size / f.height;
                //    bmpData.PixelFormat = pixfmt;
                //    bmpData.Scan0 = f.buff;
                //    _bmp.LockBits(new Rectangle(0, 0, _bmp.Width, _bmp.Height),
                //        ImageLockMode.WriteOnly | ImageLockMode.UserInputBuffer,
                //        pixfmt, bmpData);
                //    _bmp.UnlockBits(bmpData);
                //}
                //panelVideo.Invalidate();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.ToString());
            }
        }
        void OnFinished()
        {
        }

        private void btnOpenFile_Click(object sender, EventArgs e)
        {
            OpenFileDialog dlg = new OpenFileDialog();
            dlg.Multiselect = false;
            if (dlg.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            {
                FFInitSetting setting = new FFInitSetting();
                setting.fileName = dlg.FileName;
                setting.dstFramePixFmt = 1;
                if (_handle != IntPtr.Zero)
                {
                    FFAPI.FF_CloseHandle(_handle);
                    _handle = IntPtr.Zero;
                }
                _stopwatch.Reset();
                FFAPI.FF_InitFile(ref setting, out _settings, out _handle);
                FFAPI.FF_SetCallback(_handle, _onNewFrame, _onFinished);
                //SetResolution(panelVideo.Width, panelVideo.Height);
            }
        }

        private void SetResolution(int w, int h)
        {
            int srcW = _settings.width;
            int srcH = _settings.height;
            double srcRate = (double)srcW / srcH;
            double viewRate = (double)w / h;
            int dstW = -1;
            int dstH = -1;
            if (viewRate > srcRate)
            {
                dstH = h;
                dstW = (int)Math.Round(srcRate * dstH);
            }
            else
            {
                dstW = w;
                dstH = (int)Math.Round(dstW / srcRate);
            }
            FFAPI.FF_SetResolution(_handle, dstW, dstH);
        }

        private void btnRun_Click(object sender, EventArgs e)
        {
            FFAPI.FF_Run(_handle);
            _stopwatch.Start();
        }

        private void panelVideo_Resize(object sender, EventArgs e)
        {
            if (panelVideo.Width == 0 || panelVideo.Height == 0)
                return;
            SetResolution(panelVideo.Width, panelVideo.Height);
        }
    }
}
