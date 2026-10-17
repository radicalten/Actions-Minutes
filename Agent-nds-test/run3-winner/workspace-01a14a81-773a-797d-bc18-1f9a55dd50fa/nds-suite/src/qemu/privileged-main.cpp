extern "C" unsigned privileged_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=privileged_checks(d);
if(d[4]!=956 || d[5])bad=1;
semi(4,bad?"FAIL: privileged vectors\n":"PASS: 956 privileged vectors\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
