// Independent CPU-only execution of pinned Rockwrestler tests on QEMU ARM946.
// LDM/STM characterization is deliberately excluded: it contains unpredictable cases.
#include "../../vendor/rockwrestler/src9/tests/tests.h"
extern "C" unsigned semi(unsigned,const void*);
static void hex(unsigned x) {
 char s[10];for(unsigned i=0;i<8;i++)s[i]="0123456789ABCDEF"[(x>>(28-i*4))&15];s[8]='\n';s[9]=0;semi(4,s);
}
extern "C" __attribute__((noreturn)) void fail_test(unsigned n) {
 semi(4,"FAIL: Rockwrestler CPU assertion\n");hex(n);
 unsigned args[2]={0x20026,1};semi(0x20,args);for(;;){}
}
extern "C" void oracle_main() {
 void (*const groups[])()={run_tests_armv4_conditioncodes,
 run_tests_armv5_clz,run_tests_armv5_qadd_qsub,run_tests_armv5_qdadd_qdsub,
 run_tests_armv5_SMULxy,run_tests_armv5_SMLAxy,run_tests_armv5_SMULWy,
 run_tests_armv5_SMLAWy,run_tests_armv5_SMLALxy,run_tests_armv5_blx,
 run_tests_armv5_ldrpopr15};
 groups[ORACLE_GROUP]();
 semi(4,"PASS: Rockwrestler CPU group\n");
 unsigned args[2]={0x20026,0};semi(0x20,args);for(;;){}
}
