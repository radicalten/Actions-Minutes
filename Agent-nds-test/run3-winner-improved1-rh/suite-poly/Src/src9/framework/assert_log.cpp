#include "trace_layout.h"
#include "../inc.h"
extern unsigned suite_active_group();
static vu32* const journal=(vu32*)ASSERT_BASE;
extern "C" void assertion_log_init() {
 for(unsigned i=0;i<16;i++)journal[i]=0;
 journal[0]=0x41535235;journal[1]=1;journal[2]=4096;
}
extern "C" void suite_assert(unsigned condition,unsigned id,unsigned line) {
 unsigned i=journal[3];
 if(i<journal[2]) {
  vu32* record=journal+16+5*i;
  record[0]=suite_active_group();record[1]=id;record[2]=line;record[3]=condition;
  record[4]=condition?2:1;journal[3]=i+1;
 } else journal[4]=journal[4]+1;
 if(condition)fail_test(id);
}
