// Minimal freestanding support for compiler-emitted buffer initialization.
extern "C" void* memset(void* dest,int value,__SIZE_TYPE__ size) {
 volatile unsigned char* p=(volatile unsigned char*)dest;
 while(size--) *p++=(unsigned char)value;
 return dest;
}
