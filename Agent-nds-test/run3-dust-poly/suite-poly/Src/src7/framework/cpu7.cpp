#include "trace_layout.h"
#include "count.h"
extern "C" unsigned cpu_checks_for(unsigned*,unsigned),arm7_checks_for(unsigned*,unsigned);
extern "C" void cpu_trace_init(unsigned,unsigned,unsigned,unsigned),cpu_trace_plan(unsigned,unsigned),cpu_trace_complete();
extern "C" void auto_cpu7() {
 for(unsigned i=0;i<4;i++) ((volatile unsigned*)0x023ee000)[i]=0;
 volatile unsigned* p=(volatile unsigned*)0x023ef000;
 for(unsigned i=0;i<12;i++)p[i]=0;
 cpu_trace_init(TRACE7_BASE,7,CPU7_VECTOR_TOTAL,CPU_MANIFEST_CRC);
 p[0]=0x52445937; // ready for independent ARM/Thumb commands
 for(;;){
  unsigned cmd=p[8];if(!cmd)continue;
  if(cmd==3){volatile unsigned* h=(volatile unsigned*)TRACE7_BASE;if(!h[4]){h[14]=0;h[15]=0;h[8]=1;}h[13]=1;p[11]=1;return;}
  if(cmd>2){p[8]=0;continue;}
  cpu_trace_plan(p[9],p[10]);
  unsigned d[6],e[6];unsigned first=cpu_checks_for(d,cmd),extra=arm7_checks_for(e,cmd);
  if(!first && extra){first=extra;for(unsigned i=0;i<4;i++)d[i]=e[i];}
  d[4]+=e[4];d[5]+=e[5];cpu_trace_complete();
  for(unsigned i=0;i<6;i++)p[2+i]=d[i];
  p[1]=first;p[8]=0;p[0]=0x43505537;
 }
}
