// Automated adapter. Original Rockwrestler assertions remain in vendor sources.
#include "../inc.h"
#include "../tests/tests.h"
extern "C" unsigned cpu_checks(unsigned*);
struct Entry { const char* name; voidfncptr fn; };
static void cpu9() { unsigned detail[4]; unsigned n=cpu_checks(detail); if(n) fail_test(n); }
static void cpu7() {
 vu32* p=(vu32*)0x023ef000;
 unsigned timeout=1<<24;
 while(p[0]!=0x43505537 && --timeout) {}
 if(!timeout) timeout_test(0);
 if(p[1]) fail_test(p[1]);
}
static const Entry entries[]={
 {"CPU ARM9 ALU",cpu9},{"CPU ARM7 ALU",cpu7},
 {"CONDITION CODES",run_tests_armv4_conditioncodes},
 {"CLZ",run_tests_armv5_clz},
 {"QADD, QSUB",run_tests_armv5_qadd_qsub},
 {"QDADD, QDSUB",run_tests_armv5_qdadd_qdsub},
 {"SMULxy",run_tests_armv5_SMULxy},
 {"SMLAxy",run_tests_armv5_SMLAxy},
 {"SMULWy",run_tests_armv5_SMULWy},
 {"SMLAWy",run_tests_armv5_SMLAWy},
 {"SMLALxy",run_tests_armv5_SMLALxy},
 {"BLX",run_tests_armv5_blx},
 {"LDR r15, POP {r15}, LDM {r15}",run_tests_armv5_ldrpopr15},
 {"LDM / STM",run_tests_armv5_ldm_stm},
 {"IPCSYNC",run_tests_ipcsync},
 {"IPCFIFO",run_tests_ipcfifo},
 {"IPCFIFO IRQ",run_tests_ipcfifo_irq},
 {"SQRT 32",run_tests_ds_maths_sqrt_32},
 {"SQRT 64",run_tests_ds_maths_sqrt_64},
 {"DIV 32/32",run_tests_ds_maths_div_32_32},
 {"DIV 64/32",run_tests_ds_maths_div_64_32},
 {"DIV 64/64",run_tests_ds_maths_div_64_64},
 {"WRAM CNT",run_tests_wramcnt},
 {"VRAM CNT",run_tests_vramcnt},
 {"TCM",run_tests_tcm},
};
// Host patches selected (0xffffffff = all). Marker occurs once in ROM.
extern "C" { volatile unsigned suite_config[3]={0x53454c31,0x4e445341,0xffffffff}; }
// Mailbox: magic, version, state, group, assertion, completed, total.
static vu32* const box=(vu32*)0x023f0000;
static unsigned active=0,completed=0;
static void publish(unsigned state,unsigned assertion) {
 box[0]=0x4e445354; box[1]=1; box[3]=active; box[4]=assertion;
 box[5]=completed; box[6]=sizeof(entries)/sizeof(entries[0]);
 box[2]=state; // commit state last, caches disabled by Rockwrestler
}
extern "C" __attribute__((noreturn)) void auto_fail(unsigned code,unsigned kind) {
 publish(kind,code); init_iostate(); clear_screen();
 draw_string(0,0,kind==3?"FAIL":"TIMEOUT");
 draw_hex_value<8>(0,2,active); draw_hex_value<8>(0,4,code);
 for(;;) asm volatile("nop");
}
extern "C" void cpp_menu() {
 completed=0;
 unsigned selected=suite_config[2];
 // Initial ARM7 startup includes CPU checks; allow it to reach command loop.
 wait_busy(1<<21);
 for(active=0;active<sizeof(entries)/sizeof(entries[0]);++active) {
  if(selected!=0xffffffff && selected!=active) continue;
  init_iostate(); clear_screen(); draw_string(0,0,entries[active].name);
  publish(1,0); call_fncptr(entries[active].fn); ++completed;
  publish(2,0); REG_IPCSYNC=0; wait_busy(1<<20);
 }
 if(!completed) {active=selected; auto_fail(0xffffffff,3);}
 publish(5,0); init_iostate(); clear_screen(); draw_string(0,0,"SUITE COMPLETE");
 draw_hex_value<8>(0,2,completed);
 for(;;) asm volatile("nop");
}
