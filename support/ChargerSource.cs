using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

// Cache-only source read. Uses the controller's ISA mutex, never its lifetime
// ownership mutex. No vendor commands, SMBus operations or EC data writes.
static class ChargerSource {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFile(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint c,byte[] i,uint ni,byte[] o,uint no,out uint read,IntPtr p);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    static void Index(IntPtr h,ushort port,byte value) {
        if(port!=0x381 && port!=0x382)throw new IOException("Invalid index port");
        byte[] data={(byte)port,(byte)(port>>8),value};
        if(!DeviceIoControl(h,0x9C402008,data,3,null,0,out _,IntPtr.Zero))throw new IOException("Source index I/O failed");
    }
    static int Read(IntPtr h) {
        Index(h,0x381,0xC5);Index(h,0x382,0xAA);
        byte[] data={0x83,0x03,0};
        if(!DeviceIoControl(h,0x9C402004,data,3,data,3,out uint n,IntPtr.Zero)||n<1)throw new IOException("Source cache read failed");
        if((data[0]&~7)!=0)throw new IOException("Invalid source cache");
        return data[0]&3;
    }
    public static int Get() {
        using var mutex=new Mutex(false,"Global\\Access_ISABUS.HTP.Method");
        bool held=false;
        try {
            try {held=mutex.WaitOne(1000);}catch(AbandonedMutexException){held=true;}
            if(!held)throw new IOException("Source reader busy");
            var h=CreateFile(@"\\.\inpoutx64",0xC0000000,0,IntPtr.Zero,3,0x80,IntPtr.Zero);
            if(h==new IntPtr(-1))throw new IOException("Existing signed I/O driver unavailable");
            try {int source=Read(h);if(Read(h)!=source)throw new IOException("Source changed during read");return source;}
            finally {CloseHandle(h);}
        } finally {if(held)mutex.ReleaseMutex();}
    }
}
