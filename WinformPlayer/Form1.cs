using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
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
        private IntPtr _ctx = IntPtr.Zero;
        private string _fileName = string.Empty;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            if (_handle != IntPtr.Zero)
                FFAPI.FF_CloseHandle(_handle);
            base.Dispose(disposing);
        }

        void Form1_Load(object sender, EventArgs e)
        {
            FFAPI.FF_Init();
        }

        private void btnOpenFile_Click(object sender, EventArgs e)
        {
            OpenFileDialog dlg = new OpenFileDialog();
            dlg.Multiselect = false;
            if (dlg.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            {
                _fileName = dlg.FileName;
                using (Bitmap bmp = new Bitmap(_fileName))
                {
                    int err = FFAPI.FF_ScalePrepared(
                        bmp.Width,
                        bmp.Height,
                        bmp.Width / 2,
                        bmp.Height / 2,
                        out _ctx);

                    using (Graphics g = this.panelVideo.CreateGraphics())
                    {
                        g.DrawImageUnscaled(bmp, 0, 0);
                    }
                }
            }
        }

        private void btnScale_Click(object sender, EventArgs e)
        {
            using (Bitmap bmp = new Bitmap(_fileName))
            {
                IntPtr srcBuff = IntPtr.Zero;
                int srcStride = -1;
                CopyData(bmp, out srcBuff, out srcStride);
                int dstW = bmp.Width / 2;
                int dstStride = dstW * 3;
                if (dstStride % 4 != 0)
                    dstStride += (4 - dstStride % 4);
                int dstH = bmp.Height / 2;

                IntPtr outBuff = Marshal.AllocHGlobal(dstStride * dstH);

                Graphics g = this.panelVideo.CreateGraphics();
                StringBuilder sb = new StringBuilder();
                Stopwatch stopwatch = Stopwatch.StartNew();
                for (int i = 0; i < 3; i++)
                {
                    int err = FFAPI.FF_Scale(_ctx,
                        srcBuff,
                        srcStride,
                        bmp.Height,
                        outBuff,
                        dstStride);
                    sb.Append(stopwatch.ElapsedMilliseconds + " ");

                    if (err >= 0)
                    {
                        using (Bitmap newBmp = new Bitmap(dstW, dstH,
                            dstStride, PixelFormat.Format24bppRgb, outBuff))
                        {
                            g.DrawImageUnscaled(newBmp, 0, 0);
                        }
                    }
                    sb.Append(stopwatch.ElapsedMilliseconds + "\n");
                    stopwatch = Stopwatch.StartNew();
                }

                for (int i = 0; i < 3; i++)
                {
                    g.DrawImage(bmp,
                        new Rectangle(0, 0, bmp.Width / 2, bmp.Height / 2));
                    sb.Append(stopwatch.ElapsedMilliseconds + "\n");
                    stopwatch = Stopwatch.StartNew();
                }
                MessageBox.Show(sb.ToString());

                g.Dispose();
                Marshal.FreeHGlobal(srcBuff);
                Marshal.FreeHGlobal(outBuff);
            }
        }

        private void CopyData(Bitmap bmp, out IntPtr data, out int stride)
        {
            BitmapData bmpData = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);

            data = Marshal.AllocHGlobal(bmpData.Stride * bmpData.Height);

            byte[] buff = new byte[bmpData.Stride * bmpData.Height];
            Marshal.Copy(bmpData.Scan0, buff, 0, buff.Length);
            Marshal.Copy(buff, 0, data, buff.Length);

            stride = bmpData.Stride;
            bmp.UnlockBits(bmpData);
        }
    }
}
