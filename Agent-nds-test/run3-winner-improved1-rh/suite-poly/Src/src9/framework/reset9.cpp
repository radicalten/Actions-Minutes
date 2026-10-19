#include "../inc.h"
extern "C" void suite_reset9() {
 l_DIC();REG_IME=0;REG_IE=0;REG_IF=0xffffffff;
 for(unsigned i=0;i<4;i++) {
  *((vu32*)(0x040000b8+12*i))=0; // DMA channels stopped
  *((vu16*)(0x04000102+4*i))=0; // timers stopped
 }
 REG_IPCSYNC=0;REG_IPCFIFOCNT=0xc008;
 REG_WRAMCNT=3;
 REG_VRAMCNT_A=0x80;REG_VRAMCNT_B=0;REG_VRAMCNT_C=0;REG_VRAMCNT_D=0;
 REG_VRAMCNT_E=0;REG_VRAMCNT_F=0;REG_VRAMCNT_G=0;REG_VRAMCNT_H=0;REG_VRAMCNT_I=0;
 init_iostate();
}
extern "C" bool suite_reset7() {
 vu32* p=(vu32*)0x023ee000;
 unsigned epoch=p[0]+1;if(!epoch)epoch=1;
 p[0]=epoch;
 unsigned timeout=1<<23;
 while(p[1]!=epoch && --timeout) {}
 if(!timeout)return false;
 REG_IPCSYNC=0;REG_IPCFIFOCNT=0xc008;
 p[2]=epoch;
 timeout=1<<23;
 while(p[3]!=epoch && --timeout) {}
 return timeout!=0;
}
