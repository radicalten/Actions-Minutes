#include "trace_layout.h"
// Shared trace writer, unused (base=0) in semihosted QEMU execution.
static volatile unsigned* trace=0;

static unsigned input_fingerprint(unsigned id,unsigned a,unsigned b,unsigned initial) {
 unsigned h=2166136261u;
 const unsigned values[4]={id,a,b,initial};
 for(unsigned i=0;i<4;i++) {
  unsigned value=values[i];
  for(unsigned j=0;j<4;j++) {h^=value&255u;h*=16777619u;value>>=8;}
 }
 return h;
}

extern "C" void cpu_trace_init(unsigned base,unsigned cpu,unsigned total,unsigned fingerprint) {
 trace=(volatile unsigned*)base;
 if(!trace)return;
 for(unsigned i=0;i<16+TRACE_RECORD_WORDS*(total<=TRACE_CAPACITY?total:0);i++)trace[i]=0;
 trace[0]=0x43545235;trace[1]=TRACE_VERSION;trace[2]=cpu;trace[3]=total;
 trace[9]=TRACE_RECORD_BYTES;trace[10]=fingerprint;trace[12]=total>TRACE_CAPACITY?1:0;
 trace[14]=total;trace[15]=3;
}
extern "C" void cpu_trace_attach(unsigned base){trace=(volatile unsigned*)base;}
extern "C" void cpu_trace_plan(unsigned count,unsigned mask){if(trace){if(count>trace[3] || mask>3){trace[12]|=16;return;}trace[14]=count;trace[15]=mask;}}
extern "C" void cpu_trace_begin(unsigned id,unsigned fn,unsigned a,unsigned b,unsigned initial,unsigned expected,unsigned aux,unsigned mask) {
 if(!trace)return;
 if(!id || id>TRACE_CAPACITY || id>trace[3]){trace[12]|=2;return;}
 volatile unsigned* p=trace+16+TRACE_RECORD_WORDS*(id-1);
 p[3]=fn;p[4]=expected;p[5]=aux;p[6]=mask;
 p[7]=input_fingerprint(id,a,b,initial);trace[7]=id;p[0]=1;
}
extern "C" void cpu_trace_end(unsigned id,unsigned a,unsigned b,unsigned failed) {
 if(!trace)return;
 if(!id || id>TRACE_CAPACITY || id>trace[3]){trace[12]|=2;return;}
 volatile unsigned* p=trace+16+TRACE_RECORD_WORDS*(id-1);
 if(p[0]!=1){trace[12]|=4;return;}
 p[1]=a;p[2]=b;p[0]=failed?3:2;
 trace[4]=trace[4]+1;unsigned counter=failed?6:5;trace[counter]=trace[counter]+1;
}
extern "C" void cpu_trace_complete() {if(trace){trace[7]=0;trace[8]=(!trace[12] && trace[4]==trace[14]);}}
