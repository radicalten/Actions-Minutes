extern "C" unsigned remaining_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=remaining_checks(d);
if(d[4]!=512 || d[5])bad=1;
semi(4,bad?"FAIL: remaining vectors\n":"PASS: 512 remaining vectors\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
