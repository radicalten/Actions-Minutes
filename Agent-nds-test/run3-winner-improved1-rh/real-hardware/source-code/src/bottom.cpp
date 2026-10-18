#include "trace_layout.h"
// Secondary LCD text console. No libnds dependency; font retained from upstream.
// Only enabled during pure CPU groups or terminal reporting, never hardware tests.
#include "../inc.h"
#include "bottom.h"
#include "count.h"
extern u8 font_start[];
extern const char* suite_group_name(unsigned);
extern bool suite_was_resumed();
extern u16 val_IPCSYNC,val_IPCFIFOCNT,FIFO_length;
extern u32 val_IME,val_IE,val_IF,val_CPSR,val_cp15[];
static vu16* const map=(vu16*)0x0620f800;
static vu32* const box=(vu32*)0x023f0000;
static vu32* const records=(vu32*)0x023f0100;
static vu32* const ui=(vu32*)0x023f0080;
static const unsigned pages=12;
static void text(unsigned x,unsigned y,const char* s) {
 if(y>=24)return;
 while(*s && x<32) map[y*32+x++]=(unsigned char)*s++;
}
static void dec(unsigned x,unsigned y,unsigned v) {
 char b[11];unsigned n=10;b[n]=0;do {b[--n]='0'+v%10;v/=10;}while(v);text(x,y,b+n);
}
static void hex(unsigned x,unsigned y,unsigned v) {
 char b[9];b[8]=0;for(unsigned i=0;i<8;i++)b[i]="0123456789ABCDEF"[(v>>(28-4*i))&15];text(x,y,b);
}
void bottom_setup() {
 REG_POWCNT1=0x8203; // main engine on upper LCD, sub engine on lower LCD
 REG_VRAMCNT_C=0x84; // bank C -> sub BG at 06200000
 *((vu32*)0x04001000)=0; // configure before displaying
 *((vu16*)0x04001008)=31<<8; // BG0, 4bpp font at 0, map at 0xf800
 *((vu16*)0x04001010)=0;*((vu16*)0x04001012)=0; // BG0 scroll
 *((vu16*)0x04001050)=0;*((vu16*)0x0400106c)=0; // no blend/master brightness
 vu16* tiles=(vu16*)0x06200000;
 for(unsigned tile=0;tile<128;tile++)for(unsigned i=0;i<64;i+=4) {
  const u8* p=&font_start[tile*64+i];
  tiles[tile*16+i/4]=p[0]|(p[1]<<4)|(p[2]<<8)|(p[3]<<12);
 }
 vu16* pal=(vu16*)0x05000400;
 pal[0]=0x7fff;pal[1]=0x5ad6;pal[2]=0x294a;pal[3]=0x1842;
 for(unsigned i=0;i<1024;i++)map[i]=' ';
 *((vu32*)0x04001000)=(1<<16)|(1<<8); // normal display, BG0 only
 ui[0]=0x55493244;ui[1]=1;ui[2]=0;ui[3]=pages;ui[4]=1;
}
void bottom_disable() {
 *((vu32*)0x04001000)=0;REG_POWCNT1 &=~(1<<9);REG_VRAMCNT_C=0;
 ui[4]=0;
}
static const char* state(unsigned s) {
 static const char* const names[]={"WAIT","PASS","FAIL","TIME","SKIP","RUN","CRSH","HTIM"};
 return s<8?names[s]:"ERR";
}
extern "C" unsigned arm7_family_count(),arm7_family(unsigned);
extern "C" const char* arm7_family_name(unsigned);
extern "C" void arm7_info(unsigned,unsigned*);
extern "C" unsigned cpu_isa(unsigned),v5_isa(unsigned),arm7_isa(unsigned);
extern "C" unsigned cpu_family_count(),v5_family_count(),cpu_family(unsigned),v5_family(unsigned);
extern "C" const char* cpu_family_name(unsigned),*v5_family_name(unsigned);
extern "C" void cpu_info(unsigned,unsigned*),v5_info(unsigned,unsigned*);
static unsigned selected_family[2]={0,0},selected_case[2]={1,1},selected_event=0;
static bool family_mode=true;
static unsigned total(unsigned c){return c?CPU7_VECTOR_TOTAL:CPU9_VECTOR_TOTAL;}
static unsigned family(unsigned id,unsigned c){return id<=CPU_VECTOR_TOTAL?cpu_family(id):cpu_family_count()+(c?arm7_family(id):v5_family(id));}
static unsigned families(unsigned c){return cpu_family_count()+(c?arm7_family_count():v5_family_count());}
static vu32* trace(unsigned c){return (vu32*)(c?TRACE7_BASE:TRACE9_BASE);}
static bool valid_trace(unsigned c){vu32* h=trace(c);return h[0]==0x43545235 && h[1]==TRACE_VERSION && h[3]==total(c) && h[9]==TRACE_RECORD_BYTES && h[10]==CPU_MANIFEST_CRC;}
static const char* state_name(unsigned s){return s==2?"PASS":s==3?"FAIL":s==1?"IN PROGRESS":"NOT RUN";}
static void vector_page(unsigned c) {
 vu32* h=trace(c);bool valid=valid_trace(c);unsigned id=selected_case[c],f=selected_family[c];
 unsigned info[7];if(id<=CPU_VECTOR_TOTAL)cpu_info(id,info);else if(c)arm7_info(id,info);else v5_info(id,info);
 vu32* r=h+16+TRACE_RECORD_WORDS*(id-1);unsigned state=valid?(r[0]&255):0;
 if(state){info[3]=r[4];info[4]=r[5];info[5]=r[6];}
 const char* name=f<cpu_family_count()?cpu_family_name(f):(c?arm7_family_name(f-cpu_family_count()):v5_family_name(f-cpu_family_count()));
 text(0,0,c?"ARM7 VECTOR EXPLORER":"ARM9 VECTOR EXPLORER");
 text(0,1,"FAMILY");dec(7,1,f+1);text(11,1,"/");dec(13,1,families(c));
 if(valid && h[11])text(19,1,"RESTORED");
 text(0,2,name);unsigned n=0;while(name[n] && n<32)n++;if(n==32)text(0,3,name+32);
 unsigned planned=0,pass=0,fail=0,waiting=0;
 for(unsigned i=1;i<=total(c);i++)if(family(i,c)==f){planned++;unsigned s=valid?(h[16+TRACE_RECORD_WORDS*(i-1)]&255):0;if(s==2)pass++;else if(s==3)fail++;else waiting++;}
 text(0,4,"PLAN");dec(5,4,planned);text(11,4,"PASS");dec(16,4,pass);text(22,4,"FAIL");dec(27,4,fail);
 text(0,5,"UNFINISHED");dec(11,5,waiting);text(17,5,family_mode?"SELECT:FAMILY":"SELECT:CASE");
 unsigned isa=id<=CPU_VECTOR_TOTAL?cpu_isa(id):(c?arm7_isa(id):v5_isa(id));
 text(0,6,"GROUP");text(7,6,suite_group_name((c?0:2)+(isa==2?1:0)));
 text(0,7,"CASE");dec(5,7,id);text(12,7,state_name(state));
 text(0,8,"A");hex(2,8,info[0]);text(12,8,"B");hex(14,8,info[1]);
 text(0,9,"INITIAL");hex(9,9,info[2]);
 text(0,11,"PRIMARY EXPECT     OBSERVED");hex(0,12,info[3]);
 if(state>=2){hex(19,12,r[1]);text(0,13,"XOR");hex(9,13,info[3]^r[1]);}else text(19,12,"--------");
 text(0,15,"AUX EXPECT         OBSERVED");hex(0,16,info[4]);
 if(state>=2)hex(19,16,r[2]);else text(19,16,"--------");
 text(0,17,"AUX MASK");hex(9,17,info[5]);if(!info[5])text(19,17,"NOT CHECKED");
 text(0,18,"MASK XOR");if(state>=2)hex(9,18,(info[4]^r[2])&info[5]);else text(9,18,"--------");
 if(valid && (r[0]&0x100))text(0,20,"CASE: PRIOR BOOT");else if(state)text(0,20,"CASE: CURRENT BOOT");
 text(0,19,"HELPER PC");if(state)hex(11,19,r[3]);else text(11,19,"NOT EXECUTED");
 text(0,21,"UP/DN:MOVE A:FAM/CASE B:NEXT FAIL");
 ui[5]=family_mode;ui[6]=f;ui[7]=id;
}
static void assertion_page() {
 vu32* h=(vu32*)ASSERT_BASE;unsigned count=h[0]==0x41535235?h[3]:0;
 text(0,0,"C++ ASSERTION EVENT JOURNAL");text(0,2,"EXECUTED");dec(10,2,count);
 text(18,2,"DROPPED");dec(26,2,h[4]);
 if(count){selected_event%=count;vu32* r=h+16+5*selected_event;
  text(0,4,"EVENT");dec(7,4,selected_event+1);text(15,4,r[4]==1?"PASS":"FAIL");
  text(0,6,"GROUP");dec(7,6,r[0]);text(0,7,suite_group_name(r[0]));
  text(0,9,"ASSERT ID");hex(12,9,r[1]);text(0,10,"SOURCE LINE");dec(13,10,r[2]);
  text(0,12,"FAIL PREDICATE EXPECT 00000000");text(0,13,"OBSERVED");hex(12,13,r[3]);
 }
 text(0,16,"EXECUTED EVENTS ONLY");text(0,17,"OTHER CHECKS: GROUP-ONLY/UNKNOWN");
 text(0,19,"SOURCE EXPRESSIONS IN HOST HTML");text(0,21,"UP/DN:EVENT B:NEXT FAILURE");ui[8]=selected_event;
}
static void debug_keys(unsigned page,unsigned keys) {
 if(page==9 || page==10){unsigned c=page-9,n=total(c),id=selected_case[c];
  if(keys&KEY_MASK_A)family_mode=!family_mode;
  if(keys&KEY_MASK_B){if(valid_trace(c))for(unsigned i=1;i<=n;i++){unsigned candidate=(id-1+i)%n+1;if((trace(c)[16+TRACE_RECORD_WORDS*(candidate-1)]&255)==3){selected_case[c]=candidate;selected_family[c]=family(candidate,c);family_mode=false;break;}}}
  if(keys&(KEY_MASK_UP|KEY_MASK_DOWN)){
   bool next=keys&KEY_MASK_DOWN;
   if(family_mode){selected_family[c]=(selected_family[c]+(next?1:families(c)-1))%families(c);for(unsigned i=1;i<=n;i++)if(family(i,c)==selected_family[c]){selected_case[c]=i;break;}}
   else for(unsigned i=1;i<=n;i++){unsigned candidate=(id-1+(next?i:n-i))%n+1;if(family(candidate,c)==selected_family[c]){selected_case[c]=candidate;break;}}
  }
 }else if(page==11){vu32* h=(vu32*)ASSERT_BASE;unsigned n=h[0]==0x41535235?h[3]:0;if(n){
  if(keys&KEY_MASK_DOWN)selected_event=(selected_event+1)%n;
  if(keys&KEY_MASK_UP)selected_event=(selected_event+n-1)%n;
  if(keys&KEY_MASK_B)for(unsigned i=1;i<=n;i++){unsigned candidate=(selected_event+i)%n;if(h[16+5*candidate+4]==2){selected_event=candidate;break;}}
 }}
}

void bottom_render(unsigned page) {
 page%=pages;for(unsigned i=0;i<32*24;i++)map[i]=' ';
 if(page==0) {
  text(0,0,"CPU GROUP DIAGNOSTICS");
  for(unsigned group=0;group<4;group++){
   unsigned c=group<2?1:0,isa=group%2?2:1,y=2+group*5;
   unsigned planned=0,passed=0,failed=0,first=0,unrun=0;bool valid=valid_trace(c);
   for(unsigned id=1;id<=total(c);id++){
    unsigned kind=id<=CPU_VECTOR_TOTAL?cpu_isa(id):(c?arm7_isa(id):v5_isa(id));if(kind!=isa)continue;
    planned++;unsigned state=valid?(trace(c)[16+TRACE_RECORD_WORDS*(id-1)]&255):0;
    if(state==2)passed++;else if(state==3){failed++;if(!first)first=id;}else unrun++;
   }
   text(0,y,suite_group_name(group));text(23,y,state(records[group*2]));
   text(0,y+1,"PLAN");dec(5,y+1,planned);text(11,y+1,"PASS");dec(16,y+1,passed);text(22,y+1,"FAIL");dec(27,y+1,failed);
   text(0,y+2,"UNEXEC");dec(7,y+2,unrun);text(14,y+2,"FIRST BAD");dec(24,y+2,first);
  }
  text(0,22,"DETAILS: ARM9/ARM7 EXPLORERS");
 } else if(page<=2) {
  unsigned first=page==1?0:14,last=page==1?14:27;
  text(0,0,page==1?"GROUPS 00-13":"GROUPS 14-26");
  text(0,2,"ID NAME                    STATE");
  for(unsigned i=first;i<last;i++) {
   unsigned y=4+i-first;
   dec(0,y,i);const char* name=suite_group_name(i);
   for(unsigned n=0;name[n] && n<23;n++)map[y*32+3+n]=name[n];
   text(27,y,state(records[i*2]));
  }
  text(0,19,"WAIT: NOT RUN  SKIP: NOT IN PLAN");
  text(0,20,"FAIL/TIME IDS ON NEXT PAGES");
  text(0,21,"GROUPS ARE NOT OPCODE COUNTS");
 } else if(page<=5) {
  text(0,0,page==3?"FAILURES 1-9":page==4?"FAILURES 10-18":"FAILURES 19-27");
  unsigned skip=(page-3)*9,n=0,shown=0;
  for(unsigned i=0;i<27;i++)if(records[i*2]==2 || records[i*2]==3 || records[i*2]==6 || records[i*2]==7) {
   if(n++<skip)continue;if(shown==9)break;
   unsigned y=3+2*shown++;dec(0,y,i);text(3,y,suite_group_name(i));
   text(3,y+1,state(records[i*2]));text(9,y+1,"ID");hex(12,y+1,records[i*2+1]);
  }
  if(!shown)text(0,4,"NO FAILURES ON THIS PAGE");
  text(0,21,"ID / CRASH SIGNAL / TIME CAUSE");
 } else if(page==6) {
  text(0,0,"BOOT SNAPSHOTS / NOT ASSERTIONS");
  text(14,2,"ARM9");text(23,2,"ARM7");
  const char* const names[]={"IPCSYNC","FIFOCNT","FIFO WORDS","IME","IE","IF","CPSR"};
  const u32 vals[]={val_IPCSYNC,val_IPCFIFOCNT,FIFO_length,val_IME,val_IE,val_IF,val_CPSR};
  vu32* a7=(vu32*)0x02000004;
  for(unsigned i=0;i<7;i++){unsigned y=4+2*i;text(0,y,names[i]);hex(14,y,vals[i]);hex(23,y,a7[i]);}
  text(0,20,"BOOT VALUES, NOT LIVE REGISTERS");
  text(0,21,"NO EXPECTED/PASS CLAIM HERE");
 } else if(page==7) {
  text(0,0,"ARM9 CP15 BOOT SNAPSHOT");
  const char* const names[]={"CONTROL","DCACHE CFG","ICACHE CFG","WRITE BUFFER","DATA PERM","CODE PERM","DATA EXT","CODE EXT","REGION 0","REGION 1","REGION 2","REGION 3","REGION 4","REGION 5","REGION 6","REGION 7","DTCM","ITCM"};
  for(unsigned i=0;i<18;i++){text(0,i+3,names[i]);hex(17,i+3,val_cp15[i]);}
 } else if(page==8) {
  text(0,0,"COVERAGE / EXECUTION LIMITS");
  text(0,2,"ARM9 / ARM7");dec(16,2,CPU9_VECTOR_TOTAL);dec(24,2,CPU7_VECTOR_TOTAL);
  text(0,4,"ARM CASES / FUNCS");dec(20,4,CPU_ARM_VECTOR_TOTAL);dec(27,4,CPU_ARM_FUNCTION_TOTAL);
  text(0,5,"THUMB CASES / FUNCS");dec(20,5,CPU_THUMB_VECTOR_TOTAL);dec(27,5,CPU_THUMB_FUNCTION_TOTAL);
  text(0,7,"GBATEK-GUIDED; NOT EXHAUSTIVE");
  text(0,9,"QEMU CHECKS ARE HOST-SIDE");
  text(0,10,"NO EXACT ARM7 QEMU MODEL");
  text(0,12,"NO REAL DS HW VALIDATION YET");
  text(0,14,"RETURNED FAILURES CONTINUE");
  text(0,15,"HOST RESUMES CRASHES / HANGS");
  text(0,17,"SCORE = PASS / PLANNED GROUPS");
  text(0,18,"CPU COUNTS ARE SEPARATE");
  text(0,20,"BOTTOM DISABLED IN HW TESTS");
  text(0,21,"TO AVOID VRAM/IPC INTERFERENCE");
 }
 if(page==9 || page==10)vector_page(page-9);else if(page==11)assertion_page();
 text(0,23,"LEFT/RIGHT: PAGE");dec(17,23,page+1);text(20,23,"/ 12");
 ui[2]=page;
}
void bottom_idle() {
 unsigned page=0,old=~REG_KEYINPUT;
 for(;;) {
  // Do not depend on VBlank IRQ delivery after an interrupt-test failure.
  unsigned keys=~REG_KEYINPUT,pressed=keys&~old;old=keys;
  if(pressed&KEY_MASK_RIGHT){page=(page+1)%pages;bottom_render(page);}
  if(pressed&KEY_MASK_LEFT){page=(page+pages-1)%pages;bottom_render(page);}
  if(pressed&(KEY_MASK_UP|KEY_MASK_DOWN|KEY_MASK_A|KEY_MASK_B)){debug_keys(page,pressed);bottom_render(page);}
  wait_busy(4096);
 }
}
