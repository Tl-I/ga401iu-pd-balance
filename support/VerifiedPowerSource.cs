using System;
using System.Runtime.InteropServices;
using System.Threading;

// Shared source semantics for GHelper: never substitute Windows AC for
// a failed firmware read, and never turn an unknown result into Battery.
public static class VerifiedPowerSource {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr CreateFile(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll")] static extern bool DeviceIoControl(IntPtr h,uint c,byte[] i,uint ni,byte[] o,uint no,out uint read,IntPtr p);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    public static int Map(int raw) => raw==0 ? 0 : raw==2 ? 2 : raw==1 || raw==3 ? 1 : -1;
    public static int Read() {
        try {
            var h=CreateFile(@"\\.\ATKACPI",0xC0000000,3,IntPtr.Zero,3,0x80,IntPtr.Zero);
            if(h!=new IntPtr(-1))try {
                var i=new byte[16];var o=new byte[16];
                BitConverter.GetBytes(0x53545344u).CopyTo(i,0);
                BitConverter.GetBytes(8u).CopyTo(i,4);
                BitConverter.GetBytes(0x0012006cu).CopyTo(i,8);
                if(DeviceIoControl(h,0x0022240c,i,16,o,16,out uint n,IntPtr.Zero)&&n>=4) {
                    int source=Map(BitConverter.ToInt32(o,0)-65536);
                    if(source>=0)return source;
                }
            } finally {CloseHandle(h);}
            return Map(ChargerSource.Get());
        } catch {return -1;}
    }
}

// Event-driven wait while PD recovery is inapplicable (barrel/battery).
// Register before reading source, so a transition between read and wait is retained.
public sealed class ChargerSourceEvent : IDisposable {
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] delegate uint Notify(IntPtr context,uint type,IntPtr data);
    [StructLayout(LayoutKind.Sequential)] struct Subscription {public IntPtr Callback,Context;}
    [DllImport("powrprof.dll")] static extern uint PowerSettingRegisterNotification(ref Guid guid,uint flags,IntPtr recipient,out IntPtr handle);
    [DllImport("powrprof.dll")] static extern uint PowerSettingUnregisterNotification(IntPtr handle);
    readonly AutoResetEvent wake=new AutoResetEvent(false);
    readonly Notify callback;
    IntPtr memory,registration;
    public ChargerSourceEvent() {
        callback=(c,t,d)=>{wake.Set();return 0;};
        memory=Marshal.AllocHGlobal(Marshal.SizeOf<Subscription>());
        Marshal.StructureToPtr(new Subscription{Callback=Marshal.GetFunctionPointerForDelegate(callback)},memory,false);
        Guid id=new Guid("5d3e9a59-e9d5-4b00-a6bd-ff34ff516548");
        uint error=PowerSettingRegisterNotification(ref id,2,memory,out registration);
        if(error!=0){Marshal.FreeHGlobal(memory);memory=IntPtr.Zero;throw new InvalidOperationException("Power event registration failed: "+error);}
    }
    public void Wait(){wake.WaitOne();}
    public bool WaitOrStop(WaitHandle stop,int milliseconds){return WaitHandle.WaitAny(new WaitHandle[]{wake,stop},milliseconds)==1;}
    public void Dispose(){if(registration!=IntPtr.Zero)PowerSettingUnregisterNotification(registration);if(memory!=IntPtr.Zero)Marshal.FreeHGlobal(memory);GC.KeepAlive(callback);}
}
