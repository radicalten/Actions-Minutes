#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include "../src/trace_layout.h"
extern "C" void cpu_trace_init(unsigned,unsigned,unsigned,unsigned);
extern "C" void cpu_trace_begin(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
extern "C" void cpu_trace_end(unsigned,unsigned,unsigned,unsigned);
extern "C" void cpu_trace_complete();
int main(){
 void* p=mmap((void*)TRACE9_BASE,TRACE_SLOT_BYTES+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(p!=(void*)-1);assert(!mprotect((char*)p+TRACE_SLOT_BYTES,4096,PROT_NONE));
 auto h=(unsigned*)p;
 cpu_trace_init(TRACE9_BASE,9,TRACE_CAPACITY,123);
 for(unsigned i=1;i<=TRACE_CAPACITY;i++){cpu_trace_begin(i,0,0,0,0,4,5,~0u);cpu_trace_end(i,4,5,0);}
 cpu_trace_complete();assert(h[8]==1&&h[4]==TRACE_CAPACITY&&h[12]==0);
 cpu_trace_begin(TRACE_CAPACITY+1,0,0,0,0,0,0,0);assert(h[12]&2);
 cpu_trace_begin(0,0,0,0,0,0,0,0);assert(h[0]==0x43545235);
 cpu_trace_init(TRACE9_BASE,9,TRACE_CAPACITY+1,123);assert(h[12]&1);
 cpu_trace_end(TRACE_CAPACITY+1,0,0,0);assert(h[12]&2);
 cpu_trace_init(TRACE9_BASE,9,1,123);cpu_trace_end(1,0,0,0);assert(h[12]&4);
 cpu_trace_complete();assert(!h[8]&&(h[12]&4));
 munmap(p,TRACE_SLOT_BYTES+4096);
}
