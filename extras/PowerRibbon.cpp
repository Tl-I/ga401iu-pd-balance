#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dwmapi.lib")
static HWND ribbon;
static HDC memoryDC;
static HBITMAP bitmap;
static HGDIOBJ oldBitmap;
static unsigned long* pixels;
static std::vector<COLORREF> palette;
static int width,height=52,percent,leftEdge,bottomEdge;
static bool prism,preview;
static ULONGLONG started;
static COLORREF hue(double h){
 double x=1-std::abs(std::fmod(h/60,2)-1);double r=0,g=0,b=0;
 if(h<60){r=1;g=x;}else if(h<120){r=x;g=1;}else if(h<180){g=1;b=x;}else if(h<240){g=x;b=1;}else if(h<300){r=x;b=1;}else{r=1;b=x;}
 return RGB((int)(r*220+25),(int)(g*220+25),(int)(b*220+25));
}
static double ease(double x){x=std::clamp(x,0.0,1.0);return 1-std::pow(1-x,3);}
static void draw(){
 if(!preview){SYSTEM_POWER_STATUS p{};if(GetSystemPowerStatus(&p)&&p.ACLineStatus==0){DestroyWindow(ribbon);return;}}
 double ms=(double)(GetTickCount64()-started),life=preview?15000.0:3000.0;
 if(ms>=life){DestroyWindow(ribbon);return;}
 double fade=ms>life-700?(life-ms)/700:std::min(1.0,ms/220);
 int length=(int)(width*(percent/100.0)*ease(ms/650));
 // Only the spectrum drifts. Once revealed, the percentage endpoint stays fixed.
 if(prism)for(int x=0;x<width;x++)palette[x]=hue(std::fmod(360.0*x/std::max(1,width-1)+ms*0.065,360.0));
 std::fill(pixels,pixels+(size_t)width*height,0UL);
 for(int y=0;y<height;y++){
  double distance=(double)(height-1-y);
  double glow=0.36*std::exp(-0.5*distance*distance/(17.0*17.0))+0.20*std::exp(-0.5*distance*distance/4.0);
  unsigned alpha=(unsigned)(255*fade*glow);
  for(int x=0;x<length;x++){
   double edge=std::clamp((length-x)/18.0,0.0,1.0);edge=edge*edge*(3-2*edge);
   unsigned a=(unsigned)(alpha*edge);
   COLORREF c=palette[x];unsigned r=GetRValue(c)*a/255,g=GetGValue(c)*a/255,b=GetBValue(c)*a/255;
   pixels[(size_t)y*width+x]=(a<<24)|(r<<16)|(g<<8)|b;
  }
 }
 POINT origin{leftEdge,bottomEdge-height+(int)((1-ease(ms/420))*height)},src{0,0};SIZE size{width,height};
 BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};HDC screen=GetDC(nullptr);
 UpdateLayeredWindow(ribbon,screen,&origin,&size,memoryDC,&src,0,&blend,ULW_ALPHA);ReleaseDC(nullptr,screen);
}
static LRESULT CALLBACK windowProc(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_NCHITTEST)return HTTRANSPARENT;
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(!argv)return 2;
 std::wstring source=argc>1?argv[1]:L"";
 prism=source==L"barrel";if(!prism&&source!=L"pd"){LocalFree(argv);return 0;}
 SYSTEM_POWER_STATUS power{};if(!GetSystemPowerStatus(&power)||power.BatteryLifePercent>100){LocalFree(argv);return 0;}
 percent=power.BatteryLifePercent;
 if(argc>2&&std::wstring(argv[2])==L"--preview")preview=true;
 LocalFree(argv);
 // Production never shows a charging ribbon when Windows reports no AC.
 if(!preview&&power.ACLineStatus!=1)return 0;
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 POINT zero{0,0};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(zero,MONITOR_DEFAULTTOPRIMARY),&monitor);
 width=monitor.rcMonitor.right-monitor.rcMonitor.left;leftEdge=monitor.rcMonitor.left;bottomEdge=monitor.rcMonitor.bottom;
 if(width<1||width>16384)return 2;
 palette.resize(width);for(int x=0;x<width;x++)palette[x]=prism?hue(330.0*x/std::max(1,width-1)):RGB(38,230,124);
 WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"PowerRibbonWindow";RegisterClassW(&wc);
 ribbon=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,wc.lpszClassName,L"전원 연결 표시",WS_POPUP,leftEdge,bottomEdge-height,width,height,nullptr,nullptr,instance,nullptr);
 if(!ribbon)return 2;
 memoryDC=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 bitmap=CreateDIBSection(memoryDC,&info,DIB_RGB_COLORS,(void**)&pixels,nullptr,0);if(!bitmap){DeleteDC(memoryDC);DestroyWindow(ribbon);return 2;}
 oldBitmap=SelectObject(memoryDC,bitmap);started=GetTickCount64();draw();ShowWindow(ribbon,SW_SHOWNOACTIVATE);
 // Pace short-lived animation against the compositor, not coarse WM_TIMER ticks.
 MSG message{};bool running=true;
 while(running){
  while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}
  if(!running)break;
  auto frameStart=GetTickCount64();
  if(FAILED(DwmFlush()))Sleep(16);
  else if(GetTickCount64()-frameStart<4)Sleep(8); // No busy spin if DWM returns immediately.
  draw();
 }
 SelectObject(memoryDC,oldBitmap);DeleteObject(bitmap);DeleteDC(memoryDC);return 0;
}
