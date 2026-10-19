#include "trace_layout.h"
// Automated adapter. Original Rockwrestler assertions remain in vendor sources.
#include "../inc.h"
#include "../tests/tests.h"
#include "bottom.h"
#include "count.h"
extern "C" unsigned cpu_checks(unsigned*);
extern "C" unsigned v5_checks(unsigned*);
extern "C" void cpu_trace_init(unsigned,unsigned,unsigned,unsigned);
extern "C" void cpu_trace_complete();
extern "C" void suite_reset9();
extern "C" bool suite_reset7();
extern "C" void assertion_log_init();
struct Entry { const char* name; voidfncptr fn; };
static unsigned cpu9_pass=0,cpu9_fail=0,cpu7_pass=0,cpu7_fail=0;
extern "C" unsigned cpu_checks_for(unsigned*,unsigned),v5_checks_for(unsigned*,unsigned);
extern "C" void cpu_trace_plan(unsigned,unsigned);
extern "C" void cpu_trace_attach(unsigned);
static unsigned selected_plan=0xffffffff;
static bool started9=false, released7=false;
static void release7(){
 if(released7)return;
 vu32* p=(vu32*)0x023ef000;
 while(p[0]!=0x52445937 && p[0]!=0x43505537){}
 p[8]=3;while(!p[11]){}released7=true;
}
static void run9(unsigned isa) {
 unsigned mask=selected_plan==2?1:selected_plan==3?2:3;
 unsigned expected=mask==1?CPU9_ARM_TOTAL:mask==2?CPU9_THUMB_TOTAL:CPU9_VECTOR_TOTAL;
 if(!started9){
  vu32* h=(vu32*)TRACE9_BASE;
  if(h[0]!=0x43545235 || h[1]!=TRACE_VERSION || h[10]!=CPU_MANIFEST_CRC || !h[11])cpu_trace_init(TRACE9_BASE,9,CPU9_VECTOR_TOTAL,CPU_MANIFEST_CRC);
  else cpu_trace_attach(TRACE9_BASE);
  started9=true;
 }
 cpu_trace_plan(expected,mask);
 unsigned d[6],e[6];unsigned first=cpu_checks_for(d,isa),extra=v5_checks_for(e,isa);cpu_trace_complete();
 if(!first && extra){first=extra;for(unsigned i=0;i<4;i++)d[i]=e[i];}
 d[4]+=e[4];d[5]+=e[5];cpu9_pass+=d[4];cpu9_fail+=d[5];
 vu32* diag=(vu32*)0x023ef100;for(unsigned i=0;i<6;i++)diag[i]=d[i];
 if(first)fail_test(first);
}
static void run7(unsigned isa){
 vu32* p=(vu32*)0x023ef000;
 while(p[0]!=0x52445937 && p[0]!=0x43505537){}
 unsigned mask=selected_plan==0?1:selected_plan==1?2:3;
 p[9]=mask==1?CPU7_ARM_TOTAL:mask==2?CPU7_THUMB_TOTAL:CPU7_VECTOR_TOTAL;p[10]=mask;
 p[0]=0;p[8]=isa;while(p[0]!=0x43505537){}
 cpu7_pass+=p[6];cpu7_fail+=p[7];if(p[1])fail_test(p[1]);
}
static void arm7(){run7(1);}static void thumb7(){run7(2);}
static void arm9(){run9(1);}static void thumb9(){run9(2);}
static const Entry entries[]={
 {"ARM7",arm7},{"ARM7Thumb",thumb7},{"ARM9",arm9},{"ARM9Thumb",thumb9},
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
const char* suite_group_name(unsigned i) {return i<27?entries[i].name:"INVALID";}
static vu32* const records=(vu32*)0x023f0100;
// Host patches selected (0xffffffff = all). Marker occurs once in ROM.
extern "C" { volatile unsigned suite_config[3]={0x53454c31,0x4e445341,0xffffffff}; }
// Optional host-resume seed: magic[2], start, enabled, four CPU counters,
// followed by the 27 state/assertion pairs. Default ROM has resume disabled.
extern "C" { volatile unsigned resume_seed[62]={0x52534d35,0x4e445353,0,0}; }
bool suite_was_resumed(){return resume_seed[3]!=0;}
static bool in_test=false;
static unsigned terminal_kind=3;
// Protocol v3: first seven words retain their original meaning.
static vu32* const box=(vu32*)0x023f0000;
static unsigned active=0,completed=0,failed=0,planned=0,last_group=0xffffffff,last_code=0;
unsigned suite_active_group(){return active;}
extern "C" unsigned suite_call(voidfncptr);
extern "C" __attribute__((noreturn)) void suite_recover();
static unsigned count() {return sizeof(entries)/sizeof(entries[0]);}
static unsigned percent() {return planned ? completed*10000/planned : 0;}
static void publish(unsigned state,unsigned assertion) {
 box[0]=0x4e445354; box[1]=3; box[3]=active; box[4]=assertion;
 box[5]=completed; box[6]=count(); box[7]=planned; box[8]=failed;
 box[9]=planned-completed-failed; box[10]=percent();
 box[11]=(completed+failed==planned);box[12]=last_group;box[13]=last_code;
 box[14]=cpu9_pass;box[15]=cpu9_fail;box[16]=cpu7_pass;box[17]=cpu7_fail;
 box[2]=state; // commit last; upstream runtime disables caches
}
static void decimal(int x,int y,unsigned n) {
 char str[11];unsigned at=10;str[at]=0;
 do {str[--at]='0'+n%10;n/=10;} while(n);
 draw_string(x,y,str+at);
}
static void score(bool complete) {
 init_iostate();clear_screen();
 draw_string(0,0,complete?(resume_seed[3]?"SUITE FINISHED - RESUMED":"SUITE FINISHED"):"SUITE STOPPED - PARTIAL");
 draw_string(0,2,"GROUPS PASSED");decimal(16,2,completed);
 draw_string(20,2,"/");decimal(22,2,planned);
 draw_string(0,4,"GROUPS NONPASS");decimal(16,4,failed);
 draw_string(0,6,"GROUPS UNRUN");decimal(16,6,planned-completed-failed);
 draw_string(0,8,"PASS SCORE");decimal(16,8,percent()/100);
 draw_string(19,8,".");unsigned frac=percent()%100;
 char digits[4]={(char)('0'+frac/10),(char)('0'+frac%10),'%',0};draw_string(20,8,digits);
 draw_string(0,10,"SCORE USES PLANNED GROUPS");
 draw_string(0,12,"CPU9 VECTORS PASS / FAIL");decimal(0,13,cpu9_pass);draw_string(7,13,"/");decimal(9,13,cpu9_fail);
 draw_string(0,15,"CPU7 VECTORS PASS / FAIL");decimal(0,16,cpu7_pass);draw_string(7,16,"/");decimal(9,16,cpu7_fail);
 if(last_group!=0xffffffff) {
  draw_string(0,18,"LAST FAIL GROUP");decimal(17,18,last_group);
  draw_string(0,20,"ASSERTION HEX");draw_hex_value<8>(15,20,last_code);
 }
 draw_string(0,22,"UNRUN IS NOT A PASS");
 draw_string(0,23,"DETAILS BELOW - LEFT/RIGHT");
 bottom_setup();
}
extern "C" __attribute__((noreturn)) void auto_fail(unsigned code,unsigned kind) {
 ++failed;last_group=active;last_code=code;
 if(active<count()){records[active*2]=(kind==4?3:2);records[active*2+1]=code;}
 terminal_kind=kind;
 // All returned assertions/timeouts unwind; a clean baseline is rebuilt next.
 // Hard CPU hangs or native emulator crashes require the checkpointed host runner.
 if(in_test) suite_recover();
 score(completed+failed==planned);publish(kind,code);bottom_render(0);
 bottom_idle();
}
extern "C" void cpp_menu() {
 completed=failed=0;assertion_log_init();
 unsigned selected=suite_config[2];selected_plan=selected;planned=selected==0xffffffff?count():1;
 for(unsigned i=0;i<count();i++){records[i*2]=(selected==0xffffffff || selected==i)?0:4;records[i*2+1]=0;}
 unsigned start=0;
 if(resume_seed[3] && selected==0xffffffff) {
  start=resume_seed[2];bool valid=start<=count();
  for(unsigned i=0;i<count();i++) {
   unsigned state=resume_seed[8+i*2];
   if(i<start) valid=valid && (state==1 || state==2 || state==3 || state==6 || state==7);
   else valid=valid && state==0;
  }
  if(!valid){active=count();auto_fail(0xfffffff1,3);}
  cpu9_pass=resume_seed[4];cpu9_fail=resume_seed[5];cpu7_pass=resume_seed[6];cpu7_fail=resume_seed[7];
  for(unsigned i=0;i<count();i++) {
   unsigned state=resume_seed[8+i*2];records[i*2]=state;records[i*2+1]=resume_seed[9+i*2];
   if(state==1)++completed;
   else if(i<start){++failed;last_group=i;last_code=records[i*2+1];}
  }
 }
 // Initialize diagnostic area even when the ARM9 vector group is not selected.
 for(unsigned i=0;i<6;i++)((vu32*)0x023ef100)[i]=0;
 wait_busy(1<<21);
 if((resume_seed[3] && start>=2) || (selected!=0xffffffff && selected>=2))release7();
 if(resume_seed[3]) {
  vu32* gate=(vu32*)0x023ed000;gate[0]=0;publish(6,0);
  unsigned budget=1<<25;while(gate[0]!=0x52455335 && --budget) {}
 }
 if(selected!=0xffffffff && selected>=count()) {active=selected;auto_fail(0xffffffff,3);}
 for(active=start;active<count();++active) {
  if(selected!=0xffffffff && selected!=active) continue;
  if(active>=2)release7();
  suite_reset9();clear_screen();draw_string(0,0,entries[active].name);
  records[active*2]=5;publish(1,0);
  if(active<=15){bottom_setup();bottom_render(0);}else bottom_disable();
  // Give frame-based hosts a durable BEGIN checkpoint before potentially fatal IO.
  wait_busy(1<<20);
  bool needs7=active==16 || active==17 || active==18 || active==24 || active==25;
  if(needs7 && !suite_reset7()) {
   records[active*2]=3;records[active*2+1]=0xfffffff0;
   ++failed;last_group=active;last_code=0xfffffff0;terminal_kind=4;
   publish(2,last_code);continue;
  }
  in_test=true;unsigned failure=suite_call(entries[active].fn);in_test=false;
  if(!failure) {++completed;records[active*2]=1;}
  suite_reset9();publish(2,0);REG_IPCSYNC=0;wait_busy(1<<20);
 }
 release7();score(true);publish(selected!=0xffffffff && failed?terminal_kind:5,failed?last_code:0);bottom_render(0);
 bottom_idle();
}
