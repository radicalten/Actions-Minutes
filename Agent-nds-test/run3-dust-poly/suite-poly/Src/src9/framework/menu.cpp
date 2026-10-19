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
#ifdef SUITE_POLY
extern "C" unsigned run_poly_group();
#include "../poly/poly_results.h"
// Short labels for the 28-group summary table (<= 11 chars so two fit on one
// 32-column line). Order matches entries[].
static const char* const group_short[]={
 "ARM7","ARM7 THUMB","ARM9","ARM9 THUMB","COND CODES","CLZ","QADD QSUB","QDADD QDSUB",
 "SMULXY","SMLAXY","SMULWY","SMLAWY","SMLALXY","BLX","LDR R15",  "LDM / STM",
 "IPCSYNC","IPCFIFO","IPCFIFO IRQ","SQRT 32","SQRT 64","DIV 32/32","DIV 64/32",
 "DIV 64/64","WRAM CNT","VRAM CNT","TCM","POLY RASTER"};
#endif
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
#ifdef SUITE_POLY
 {"POLY RASTER",(voidfncptr)run_poly_group},
#endif
};
const char* suite_group_name(unsigned i) {
#ifdef SUITE_POLY
 unsigned n=sizeof(entries)/sizeof(entries[0]);
 return i<n?entries[i].name:"INVALID";
#else
 return i<27?entries[i].name:"INVALID";
#endif
}
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
#ifdef SUITE_POLY
// "NN X NAME" cell: group number, result letter, short name.
static void group_cell(int x,int y,unsigned i) {
 // WAIT PASS FAIL TIME SKIP RUN CRSH HTIM (same order as bottom.cpp)
 static const char letters[8]={'.','P','F','T','S','R','C','H'};
 char b[20];int k=0;
 b[k++]=(char)('0'+(i/10)%10);b[k++]=(char)('0'+i%10);
 b[k++]=' ';b[k++]=letters[records[i*2]&7];b[k++]=' ';
 unsigned n=sizeof(group_short)/sizeof(group_short[0]);
 const char* nm=i<n?group_short[i]:"?";
 for(int c=0;nm[c] && c<11;c++)b[k++]=nm[c];
 b[k]=0;draw_string(x,y,b);
}
// "96.42%" from the basis-point score.
static void score_text(int x,int y) {
 unsigned p=percent();
 char b[8]={(char)('0'+(p/1000)%10),(char)('0'+(p/100)%10),'.',
            (char)('0'+(p/10)%10),(char)('0'+p%10),'%',0};
 draw_string(x,y,b);
}
// "1/77" counter.
static void pair(int x,int y,unsigned a,unsigned b) {
 char s[12];int lo=10;
 do{s[lo--]=(char)('0'+b%10);b/=10;}while(b);
 s[lo--]='/';
 do{s[lo--]=(char)('0'+a%10);a/=10;}while(a);
 draw_string(x,y,&s[lo+1]);
}
static void score(bool complete) {
 init_iostate();clear_screen();
 draw_string(0,0,complete?(resume_seed[3]?"SUITE-POLY RESULT - RESUMED":"SUITE-POLY RESULT")
                         :"SUITE-POLY RESULT - PARTIAL");
 draw_string(19,0,"POLY 1.0.2-B");

 // Group counters and the pass score.
 draw_string(0,1,"PASS");decimal(5,1,completed);
 draw_string(10,1,"FAIL");decimal(15,1,failed);
 draw_string(20,1,"UNRUN");decimal(26,1,planned-completed-failed);
 draw_string(0,2,"SCORE");score_text(6,2);
 draw_string(13,2,"CPU9");pair(18,2,cpu9_pass,cpu9_fail);
 draw_string(22,2,"CPU7");pair(27,2,cpu7_pass,cpu7_fail);

 vu32* poly=(vu32*)POLY_RESULT_BASE;
 if(poly[PH_MAGIC]==POLY_RESULT_MAGIC) {
  draw_string(0,3,"POLY");pair(5,3,poly[PH_PASS],poly[PH_TEST_COUNT]);
  draw_string(11,3,"FAIL");decimal(16,3,poly[PH_FAIL]);
  unsigned ff=poly[PH_FIRST_FAIL];
  draw_string(20,3,"1ST");
  if(ff==0xffffffffu)draw_string(24,3,"NONE");else decimal(24,3,ff);
  draw_string(0,4,"ERR");decimal(4,4,poly[PH_ERROR]);
  draw_string(7,4,"M");decimal(9,4,poly[PH_MISSING_PX]);
  draw_string(14,4,"O");decimal(16,4,poly[PH_OVERDRAW_PX]);
  draw_string(21,4,"W");decimal(23,4,poly[PH_WRONGCOLOR_PX]);
 } else {
  draw_string(0,3,"POLY GROUP NOT RUN");
 }

 // Every group with its result letter: 14 rows, two columns.
 {
  unsigned n=count();
  for(unsigned i=0;i<n && i<28;i++)
   group_cell((i<14)?0:16,5+(int)(i%14),i);
 }

 if(last_group!=0xffffffff) {
  draw_string(0,20,"FAIL");decimal(5,20,last_group);draw_string(8,20,"CODE");
  draw_hex_value<8>(13,20,last_code);
 } else {
  draw_string(0,20,"NO GROUP FAILURES");
 }
 draw_string(0,21,"P PASS F FAIL T TIME S SKIP");
 draw_string(0,22,"R RUN C CRSH H HTIM . WAIT");
 draw_string(0,23,"DETAIL: BOTTOM SCREEN L/R");
 bottom_setup();
}
#else
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
#endif
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
#ifdef SUITE_POLY
  // The host resume seed carries 27 state/assertion pairs. Groups appended by
  // optional modules (polygon raster) are not seeded and always run fresh.
  unsigned seeded=count();if(seeded>27)seeded=27;
  for(unsigned i=0;i<seeded;i++) {
#else
  for(unsigned i=0;i<count();i++) {
#endif
   unsigned state=resume_seed[8+i*2];
   if(i<start) valid=valid && (state==1 || state==2 || state==3 || state==6 || state==7);
   else valid=valid && state==0;
  }
  if(!valid){active=count();auto_fail(0xfffffff1,3);}
  cpu9_pass=resume_seed[4];cpu9_fail=resume_seed[5];cpu7_pass=resume_seed[6];cpu7_fail=resume_seed[7];
  for(unsigned i=0;i<count();i++) {
#ifdef SUITE_POLY
   unsigned state=i<seeded ? resume_seed[8+i*2] : 0;
   records[i*2]=state;records[i*2+1]=i<seeded?resume_seed[9+i*2]:0;
#else
   unsigned state=resume_seed[8+i*2];records[i*2]=state;records[i*2+1]=resume_seed[9+i*2];
#endif
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
 release7();score(true);publish(selected!=0xffffffff && failed?terminal_kind:5,failed?last_code:0);
#ifdef SUITE_POLY
 // Land on the polygon report when that group was the one that failed.
 if(last_group==count()-1) bottom_render(12); else bottom_render(0);
#else
 bottom_render(0);
#endif
 bottom_idle();
}
