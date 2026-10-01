#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <deque>
#include <stdlib.h>
#include <ctype.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT 0
#define SERIAL_8N1 0
extern uint32_t g_ms;
inline uint32_t millis(){return g_ms;}
inline void delay(uint32_t ms){g_ms+=ms;}
extern int g_pin[64];
inline void pinMode(int,int){}
inline void digitalWrite(int p,int v){g_pin[p]=v;}
inline int digitalRead(int p){return g_pin[p];}
struct HardwareSerial{
  std::deque<char> q;
  HardwareSerial(int){}
  void begin(uint32_t,int,int,int){}
  void setRxBufferSize(int){}
  int available(){return (int)q.size();}
  int read(){int c=q.front();q.pop_front();return c;}
};
struct SerialClass{
  std::deque<char> in;
  void begin(int){}
  int available(){return (int)in.size();}
  int read(){int c=in.front();in.pop_front();return c;}
  void println(const char*s){printf("%s\n",s);}
  void print(const char*s){printf("%s",s);}
  void printf(const char*f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);}
};
extern SerialClass Serial;
struct TwoWire{ void begin(int,int){} void beginTransmission(int){} int endTransmission(){return 0;} };
extern TwoWire Wire;
