extern "C" unsigned cp15_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=cp15_oracle_checks(d);if(d[4]!=208 || d[5])bad=1;
semi(4,bad?"FAIL: CP15 alias vectors\n":"PASS: 208 CP15 alias vectors\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
