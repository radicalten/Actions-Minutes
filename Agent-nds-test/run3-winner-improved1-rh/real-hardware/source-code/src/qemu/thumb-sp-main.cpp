extern "C" unsigned thumb_oracle_checks(unsigned*);
extern "C" unsigned semi(unsigned,const void*);
extern "C" void oracle_main(){unsigned d[6];unsigned bad=thumb_oracle_checks(d);if(d[4]!=192||d[5])bad=1;
semi(4,bad?"FAIL: Thumb SP\n":"PASS: 192 Thumb SP\n");unsigned args[2]={0x20026,bad?1u:0u};semi(0x20,args);for(;;){}}
