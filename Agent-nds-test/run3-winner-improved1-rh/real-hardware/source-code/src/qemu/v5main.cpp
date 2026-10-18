extern "C" unsigned v5_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=v5_oracle_checks(d);if(d[4]!=2012 || d[5])bad=1;
semi(4,bad?"FAIL: ARM9 portable vectors\n":"PASS: 2012 ARM9 portable vectors\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
