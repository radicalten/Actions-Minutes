extern "C" unsigned cpu_checks(unsigned*);
extern "C" void auto_cpu7() {
 volatile unsigned* p=(volatile unsigned*)0x023ef000;
 unsigned detail[4]={0,0,0,0};
 unsigned result=cpu_checks(detail);
 for(unsigned i=0;i<4;i++) p[2+i]=detail[i];
 p[1]=result; p[0]=0x43505537;
}
