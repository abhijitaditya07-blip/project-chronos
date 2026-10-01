#include "Arduino.h"
#include "RTClib.h"
uint32_t g_ms=0; int g_pin[64]; SerialClass Serial; TwoWire Wire; int64_t g_rtcOffset=0; bool g_lost=true; uint32_t g_base=1790000000; // Sep 2026
#ifndef FIRMWARE_PATH
#define FIRMWARE_PATH "../../firmware/chronos/chronos.ino"
#endif
#include FIRMWARE_PATH
static void push(HardwareSerial&s,const char*body){ uint8_t cs=0; for(const char*p=body;*p;p++)cs^=*p; char b[160]; snprintf(b,sizeof b,"$%s*%02X\r\n",body,cs); for(char*p=b;*p;p++)s.q.push_back(*p);}
static bool gpsOn=true, noFix=false; static double latOff=0; static int timeOff=0; static int sats=8; static int snrMode=0;
static void epoch(){
  if(!gpsOn) return;
  uint32_t t=g_base+g_ms/1000+timeOff; DateTime d(t);
  // convert t -> y m d
  long z=(long)(t/86400)+719468; long era=z/146097; unsigned doe=z-era*146097; unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365; long y=yoe+era*400; unsigned doy=doe-(365*yoe+yoe/4-yoe/100); unsigned mp=(5*doy+2)/153; unsigned dd=doy-(153*mp+2)/5+1; unsigned m=mp<10?mp+3:mp-9; y+=(m<=2);
  char body[120]; double lat=12.9716+latOff; int ld=(int)lat; double lm=(lat-ld)*60; 
  static int jit=0; jit=(jit+1)%5; double lonm=(77.5946-77)*60+jit*0.00001;
  char lats[20]; snprintf(lats,sizeof lats,"%02d%07.4f",ld,lm); char lons[20]; snprintf(lons,sizeof lons,"07%07.4f",lonm+0); // 077 deg
  snprintf(lons,sizeof lons,"077%07.4f",lonm - 0);
  lons[0]='0';lons[1]='7';lons[2]='7'; char lonf[24]; snprintf(lonf,sizeof lonf,"077%07.4f",lonm);
  if(noFix){ snprintf(body,sizeof body,"GPRMC,%02d%02d%02d.00,V,,,,,,,%02u%02u%02u,,,N",d.hour(),d.minute(),d.second(),dd,m,(unsigned)(y%100)); push(GPSSerial,body);
    push(GPSSerial,"GPGGA,,,,,,0,00,99.99,,,,,,"); }
  else { snprintf(body,sizeof body,"GPRMC,%02d%02d%02d.00,A,%s,N,%s,E,0.10,0.0,%02u%02u%02u,,,A",d.hour(),d.minute(),d.second(),lats,lonf,dd,m,(unsigned)(y%100)); push(GPSSerial,body);
    snprintf(body,sizeof body,"GPGGA,%02d%02d%02d.00,%s,N,%s,E,1,%02d,1.1,900.0,M,-80.0,M,,",d.hour(),d.minute(),d.second(),lats,lonf,sats); push(GPSSerial,body);
    // GSV: 8 sats, varying SNR or uniform
    char g1[120],g2[120]; int s[8]; for(int i=0;i<8;i++) s[i]= snrMode? 40 : 25+i*3;
    snprintf(g1,sizeof g1,"GPGSV,2,1,08,01,40,083,%d,02,17,308,%d,03,07,344,%d,04,22,228,%d",s[0],s[1],s[2],s[3]); push(GPSSerial,g1);
    snprintf(g2,sizeof g2,"GPGSV,2,2,08,05,40,083,%d,06,17,308,%d,07,07,344,%d,08,22,228,%d",s[4],s[5],s[6],s[7]); push(GPSSerial,g2); }
}
static void run(int seconds){ for(int i=0;i<seconds*100;i++){ g_ms+=10; if(g_ms%1000==300) epoch(); loop(); } }
static void press(int pin,int ms){ g_pin[pin]=LOW; run(0); for(int i=0;i<ms/10;i++){g_ms+=10; if(g_ms%1000==300) epoch(); loop();} g_pin[pin]=HIGH; for(int i=0;i<5;i++){g_ms+=10;loop();} }
static void fire(int n){ int g=0; while(simSelected!=n && g++<6) press(PIN_BTN_SIM,100); press(PIN_BTN_SIM,1600); }
static int g_fail=0, g_total=0;
#define CHECK(c) do{ g_total++; bool ok_=(c); if(!ok_) g_fail++; printf("CHECK %-58s %s\n",#c,ok_?"OK":"**FAIL**"); }while(0)
int main(){
  for(int i=0;i<64;i++) g_pin[i]=HIGH;
  g_ms=1000; setup();
  printf("---- phase 1: boot, RTC lost power, GPS good\n"); run(40);
  CHECK(state==ST_INIT); CHECK(needSync); CHECK(baselineReady);
  printf("---- phase 2: hold ACK 3 s to provision\n"); press(PIN_BTN_ACK,3200); run(30);
  CHECK(state==ST_TRUSTED);
  printf("---- phase 3: normal 30 s\n"); run(30); CHECK(state==ST_TRUSTED); CHECK(lastMask==0); CHECK(simActive==SIM_NONE);
#ifdef RELAY_VARIANT
  CHECK(relayClosed);
#else
  CHECK(!relayClosed);
#endif
  printf("---- phase 4: SIM time jump\n"); fire(1); run(8); CHECK(state==ST_UNSAFE); CHECK(latched);
#ifdef RELAY_VARIANT
  CHECK(!relayClosed);
#endif
  run(10); CHECK(simActive==SIM_NONE); CHECK(state==ST_UNSAFE);
  press(PIN_BTN_ACK,100); CHECK(state==ST_WARNING); run(15); CHECK(state==ST_WARNING); run(15); CHECK(state==ST_TRUSTED);
  printf("---- phase 5: SIM position jump\n"); run(2); fire(2); run(8); CHECK(state==ST_UNSAFE);
  run(10); press(PIN_BTN_ACK,100); run(30); CHECK(state==ST_TRUSTED);
  printf("---- phase 6: SIM signal loss (fault, not anomaly)\n"); fire(3); run(8); CHECK(state==ST_WARNING); CHECK(!latched);
  run(50); CHECK(state==ST_TRUSTED);
  printf("---- phase 7: SIM nmea burst\n"); fire(4); run(8); CHECK(state==ST_WARNING); run(45); CHECK(state==ST_TRUSTED);
  printf("---- phase 8: SIM slow drift\n"); fire(5); run(25); CHECK(state==ST_WARNING); run(15); CHECK(state==ST_UNSAFE);
  run(20); press(PIN_BTN_ACK,100); run(30); CHECK(state==ST_TRUSTED);
  printf("---- phase 9: real-ish receiver silence\n"); gpsOn=false; run(10); CHECK(state==ST_WARNING); run(35); CHECK(state==ST_UNSAFE); CHECK(!latched);
  gpsOn=true; run(10); CHECK(state==ST_WARNING); run(30); CHECK(state==ST_TRUSTED);
  printf("---- phase 10: uniform SNR heuristic\n"); snrMode=1; run(12); CHECK(state==ST_WARNING); snrMode=0; run(40); CHECK(state==ST_TRUSTED);
  printf("---- phase 11: real position step 300 m north\n"); latOff=0.0027; run(8); CHECK(state==ST_UNSAFE);
  printf("\n%d checks, %d failed\n",g_total,g_fail);
  return g_fail?1:0;
}
