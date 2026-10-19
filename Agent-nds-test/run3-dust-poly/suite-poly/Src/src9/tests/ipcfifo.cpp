// tests IPCFIFO (not entirely complete)
#include "../inc.h"

void run_tests_ipcfifo()
{
    REG_IPCFIFOCNT = 1 << 15; // enable FIFO
    REG_IPCSYNC = 12 << 8; // tell ARM7 to start IPCFIFO test

    // ARM7 starts by sending 0x12345678
    suite_assert((unsigned)(read_from_IPCFIFO(0x000) != 0x12345678), 0x001, 10);

    // write 0 to IPCSYNC
    REG_IPCSYNC = 0;

    suite_assert((unsigned)(!(REG_IPCFIFOCNT & 1)), 0x002, 15); // send empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 1)), 0x003, 16); // send full = 0
    suite_assert((unsigned)(!(REG_IPCFIFOCNT & (1 << 8))), 0x004, 17); // receive empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 9)), 0x005, 18); // receive full = 0
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 14)), 0x006, 19); // error = 0

    // send three words
    REG_IPCFIFOSEND = 0x1844CCAB;
    REG_IPCFIFOSEND = 0xB914A201;
    REG_IPCFIFOSEND = 0x1844CCAC;

    // wait until "receive full" bit is on
    wait_bit_value(0x007, IPCFIFOCNT, 9, true);

    suite_assert((unsigned)(!(REG_IPCFIFOCNT & 1)), 0x008, 29); // send empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 1)), 0x009, 30); // send full = 0
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 8)), 0x00A, 31); // receive empty = 0
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 14)), 0x00B, 32); // error = 0

    // read 16 words, make sure they match
    for(int i = 0; i < 16; i++)
    {
        u32 word = REG_IPCFIFORECV;
        suite_assert((unsigned)(word != (0x11000000 + i)), 0x010 + i, 38);
    }

    suite_assert((unsigned)(!(REG_IPCFIFOCNT & 1)), 0x020, 41); // send empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 1)), 0x021, 42); // send full = 0
    suite_assert((unsigned)(!(REG_IPCFIFOCNT & (1 << 8))), 0x022, 43); // receive empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 9)), 0x023, 44); // receive full = 0
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 14)), 0x024, 45); // error = 0

    // read from empty queue: should read value that was last read, and set error bit
    u32 word = REG_IPCFIFORECV;
    suite_assert((unsigned)(word != 0x1100000F), 0x025, 49); // seems to fail on desmume/melonDS, passes on no$gba
    
    suite_assert((unsigned)(!(REG_IPCFIFOCNT & 1)), 0x026, 51); // send empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 1)), 0x027, 52); // send full = 0
    suite_assert((unsigned)(!(REG_IPCFIFOCNT & (1 << 8))), 0x028, 53); // receive empty = 1
    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 9)), 0x029, 54); // receive full = 0
    suite_assert((unsigned)(!(REG_IPCFIFOCNT & (1 << 14))), 0x02A, 55); // error = 1

    // write 0 to error bit, should NOT turn it off
    REG_IPCFIFOCNT = REG_IPCFIFOCNT & ~(1 << 14);
    
    suite_assert((unsigned)(!(REG_IPCFIFOCNT & (1 << 14))), 0x02B, 60); // error = 1

    // acknowledge it by writing a 1
    REG_IPCFIFOCNT = REG_IPCFIFOCNT; // don't optimize this out

    suite_assert((unsigned)(REG_IPCFIFOCNT & (1 << 14)), 0x02C, 65); // error = 0

    REG_IPCSYNC = 0;
}
