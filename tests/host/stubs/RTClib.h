#pragma once
#include "Arduino.h"
class DateTime{
 public:
  uint32_t t;
  DateTime(uint32_t s=0):t(s){}
  static long days(int y,int m,int d){ y-= m<=2; long era=(y>=0?y:y-399)/400; unsigned yoe=y-era*400; unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1; unsigned doe=yoe*365+yoe/4-yoe/100+doy; return era*146097+(long)doe-719468;}
  DateTime(int y,int mo,int d,int h,int mi,int s){ t=(uint32_t)(days(y,mo,d)*86400L+h*3600L+mi*60L+s);}
  uint32_t unixtime()const{return t;}
  int hour()const{return (t%86400)/3600;} int minute()const{return (t%3600)/60;} int second()const{return t%60;}
  int year()const{ long z=(long)(t/86400)+719468; long era=z/146097; unsigned doe=z-era*146097; unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365; long y=yoe+era*400; unsigned doy=doe-(365*yoe+yoe/4-yoe/100); unsigned mp=(5*doy+2)/153; unsigned m=mp<10?mp+3:mp-9; return (int)(y+(m<=2));}
};
extern int64_t g_rtcOffset; extern bool g_lost; extern uint32_t g_ms; extern uint32_t g_base;
class RTC_DS3231{ public:
  bool begin(){return true;}
  bool lostPower(){return g_lost;}
  DateTime now(){ return DateTime((uint32_t)((int64_t)g_base + g_ms/1000 + g_rtcOffset)); }
  void adjust(const DateTime&d){ g_rtcOffset=(int64_t)d.unixtime()-(int64_t)(g_base+g_ms/1000); g_lost=false; }
};
