#include "kernel.h"
/* Real-time clock (CMOS) -> calendar time and Unix time. */
static int rd(int reg){ outb(0x70,reg); return inb(0x71); }
static int bcd(int v,int b){ return b? v : (v&0x0F)+((v>>4)&0x0F)*10; }
static void read_once(int *f){
  int tries=0; while((rd(0x0A)&0x80) && tries++<10000);
  int b=rd(0x0B)&4;                                         /* binary mode? */
  f[0]=bcd(rd(0),b); f[1]=bcd(rd(2),b); f[2]=bcd(rd(4)&0x7F,b); f[3]=bcd(rd(7),b); f[4]=bcd(rd(8),b); f[5]=bcd(rd(9),b);
  int cen=bcd(rd(0x32),b); f[6]=(cen>=19&&cen<=22)? cen : 20;
}
/* sec, min, hour, day, month, year(2 digits), century */
u64 rtc_unix(void){
  int a[7], c[7]; read_once(a); for(int i=0;i<4;i++){ read_once(c); if(!memcmp(a,c,sizeof a)) break; memcpy(a,c,sizeof a); }
  int y=a[6]*100+a[5], m=a[4], d=a[3];
  y-= m<=2; int era=(y>=0? y : y-399)/400; unsigned yoe=y-era*400;
  unsigned doy=(153*(m+(m>2? -3 : 9))+2)/5+d-1, doe=yoe*365+yoe/4-yoe/100+doy;
  i64 days=(i64)era*146097+doe-719468;
  return (u64)(days*86400+a[2]*3600+a[1]*60+a[0]);
}
/* current Unix time, advanced by the timer between RTC reads */
u64 time_now(void){
  static u64 base; static u32 t0;
  if(!base){ base=rtc_unix(); t0=ticks; }
  return base+(ticks-t0)/1000;
}
void unix_to_civil(u64 t, int *y, int *mo, int *d, int *h, int *mi, int *s, int *wd){
  i64 days=t/86400; u32 rem=t%86400; *h=rem/3600; *mi=(rem%3600)/60; *s=rem%60; *wd=(int)((days+4)%7);
  days+=719468; i64 era=(days>=0? days : days-146096)/146097; unsigned doe=days-era*146097;
  unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365; i64 yy=yoe+era*400; unsigned doy=doe-(365*yoe+yoe/4-yoe/100), mp=(5*doy+2)/153;
  *d=doy-(153*mp+2)/5+1; *mo=mp<10? mp+3 : mp-9; *y=yy+(*mo<=2);
}
