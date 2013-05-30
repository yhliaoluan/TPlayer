using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace WinformPlayer
{
    public delegate void OnNewFrame(IntPtr frame);
    public delegate void OnFinished();

    [StructLayout(LayoutKind.Sequential)]
    public struct FF_SETTINGS
    {
        [MarshalAs(UnmanagedType.I4)]
        public int width;

        [MarshalAs(UnmanagedType.I4)]
        public int height;

        [MarshalAs(UnmanagedType.I4)]
        public int fpsNum;

        [MarshalAs(UnmanagedType.I4)]
        public int fpsDen;

        [MarshalAs(UnmanagedType.I4)]
        public int timebaseNum;

        [MarshalAs(UnmanagedType.I4)]
        public int timebaseDen;

        [MarshalAs(UnmanagedType.I8)]
        public long duration;

        [MarshalAs(UnmanagedType.I8)]
        public long totalFrames;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string codecName;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct FF_INIT_SETTING
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string fileName;

        [MarshalAs(UnmanagedType.I4)]
        public int dstFramePixFmt;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FF_FRAME : IDisposable
    {
        public IntPtr buff;

        [MarshalAs(UnmanagedType.I8)]
        public long pos;

        [MarshalAs(UnmanagedType.I4)]
        public int keyFrame;

        [MarshalAs(UnmanagedType.I8)]
        public long bets;

        [MarshalAs(UnmanagedType.I8)]
        public long pts;

        [MarshalAs(UnmanagedType.I8)]
        public long dts;

        [MarshalAs(UnmanagedType.I4)]
        public int oriSize;

        [MarshalAs(UnmanagedType.I4)]
        public int size;

        public double time;

        public int width;
        public int height;

        public void Dispose()
        {
            if (buff != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(buff);
                buff = IntPtr.Zero;
            }
        }
    }

    class FFAPI
    {
        [DllImport("TPlayer.dll", CharSet = CharSet.Unicode)]
        public static extern int FF_Init();

        [DllImport("TPlayer.dll", CharSet = CharSet.Unicode)]
        public static extern int FF_InitFile(ref FF_INIT_SETTING setting,
            out FF_SETTINGS settings,
            out IntPtr handle);

        [DllImport("TPlayer.dll", CharSet = CharSet.Unicode)]
        public static extern int FF_CloseHandle(IntPtr handle);

        [DllImport("TPlayer.dll")]
        public static extern int FF_ReadNextFrame(
            IntPtr handle);

        [DllImport("TPlayer.dll")]
        public static extern int FF_SeekTime(
            IntPtr handle,
            double time);

        [DllImport("TPlayer.dll")]
        public static extern int FF_SetCallback(IntPtr handle, OnNewFrame cb, OnFinished cb2);

        [DllImport("TPlayer.dll")]
        public static extern int FF_Run(IntPtr handle);

        [DllImport("TPlayer.dll")]
        public static extern int FF_Pause(IntPtr handle);

        [DllImport("TPlayer.dll")]
        public static extern int FF_Stop(IntPtr handle);

        [DllImport("TPlayer.dll")]
        public static extern int FF_GetCurFrame(IntPtr handle);

        [DllImport("TPlayer.dll")]
        public static extern int FF_ScalePrepared(int srcW,
							   int srcH,
							   int dstW,
							   int dstH,
							   out IntPtr ppCtx);

        [DllImport("TPlayer.dll")]
        public static extern int FF_Scale(IntPtr pCtx,
					   IntPtr buff,
					   int srcStride,
					   int srcH,
					   IntPtr outBuff,
					   int dstStride);

        [DllImport("TPlayer.dll")]
        public static extern int FF_SetResolution(IntPtr handle, int width, int height);
    }
}
