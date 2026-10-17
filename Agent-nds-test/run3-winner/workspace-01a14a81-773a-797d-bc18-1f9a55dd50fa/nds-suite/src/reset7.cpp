#include "../inc.h"
static vu32* const ctl=(vu32*)0x023ee000;
extern "C" void service_restart7() __attribute__((noreturn));
extern "C" void service_poll7() {
 unsigned epoch=ctl[0];
 if(epoch==ctl[1])return;
 l_DIC();REG_IME=0;REG_IE=0;REG_IF=0xffffffff;
 REG_IPCSYNC=0;REG_IPCFIFOCNT=0xc008;
 // Acknowledge only after quiescing IPC. Wait for ARM9's clean baseline.
 ctl[1]=epoch;
 while(ctl[2]!=epoch) asm volatile("nop");
 service_restart7();
}
