extern "C" unsigned thumb_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=thumb_oracle_checks(d);if(d[4]!=800||d[5])bad=1;
semi(4,bad?"FAIL: Thumb edges\n":"PASS: 800 Thumb edges\n");unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){}}
