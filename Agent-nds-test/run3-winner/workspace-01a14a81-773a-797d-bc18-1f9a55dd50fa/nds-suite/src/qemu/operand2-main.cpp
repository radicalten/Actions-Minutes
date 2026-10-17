extern "C" unsigned operand2_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=operand2_checks(d);
if(d[4]!=256 || d[5])bad=1;
semi(4,bad?"FAIL: operand2 vectors\n":"PASS: 256 operand2 vectors\n");
unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){} }
