#include "../generated/count.h"
extern "C" unsigned cpu_checks(unsigned*);
extern "C" unsigned semi(unsigned, const void*);
static void hex(unsigned x) {
 char s[10]; for(unsigned i=0;i<8;i++) s[i]="0123456789ABCDEF"[(x>>(28-4*i))&15];s[8]='\n';s[9]=0;semi(4,s);
}
extern "C" void oracle_main() {
 unsigned detail[6]; unsigned result=cpu_checks(detail);
 semi(4,result ? "FAIL: CPU vectors\n" : "PASS: " CPU_VECTOR_COUNT " CPU vectors\n");
 if(result) { hex(result); for(unsigned i=0;i<4;i++) hex(detail[i]); }
 unsigned args[2]={0x20026,result ? 1u : 0u};
 semi(0x20,args);
 for(;;) {}
}
