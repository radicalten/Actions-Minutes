#include "../generated/count.h"
extern "C" unsigned cpu_checks(unsigned*);
extern "C" unsigned semi(unsigned, const void*);
extern "C" void oracle_main() {
 unsigned detail[4]; unsigned result=cpu_checks(detail);
 semi(4,result ? "FAIL: CPU vectors\n" : "PASS: " CPU_VECTOR_COUNT " CPU vectors\n");
 unsigned args[2]={0x20026,result ? 1u : 0u};
 semi(0x20,args);
 for(;;) {}
}
