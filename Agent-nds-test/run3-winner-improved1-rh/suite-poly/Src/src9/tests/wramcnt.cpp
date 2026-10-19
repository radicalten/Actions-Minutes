// tests WRAMCNT/allocating the 32 KB memory block (partially) to ARM7
#include "../inc.h"

void run_tests_wramcnt()
{
    // get ARM7 ready for read/write commands
    ARM7_start_rw();

    // NDS9 - WRAMCNT - 0: 32 KB (ARM9)
    //                  1: second half (ARM9),  first half (ARM7)   
    //                  2:  first half (ARM9), second half (ARM7)
    //                  3: 32 KB (ARM7)
    // deallocation: ARM9: undefined (3******)
    //               ARM7: area is 03000000-037FFFFF, not allocated -> mirror of ARM7-WRAM (03800000-03FFFFFF)

    // allocate all to ARM9
    REG_WRAMCNT = 0;

    // write magic values at start/end of each 16 KB section
    *((vu32*)0x3000000) = SHWRAM_START1;
    *((vu32*)0x3003FFC) = SHWRAM_END1;
    *((vu32*)0x3004000) = SHWRAM_START2;
    *((vu32*)0x3007FFC) = SHWRAM_END2;

    // try to read it back
    suite_assert((unsigned)(*((vu32*)0x3000000) != SHWRAM_START1), 0x000, 26);
    suite_assert((unsigned)(*((vu32*)0x3003FFC) != SHWRAM_END1), 0x001, 27);
    suite_assert((unsigned)(*((vu32*)0x3004000) != SHWRAM_START2), 0x002, 28);
    suite_assert((unsigned)(*((vu32*)0x3007FFC) != SHWRAM_END2), 0x003, 29);
    // + from a mirror
    suite_assert((unsigned)(*((vu32*)0x3020000) != SHWRAM_START1), 0x004, 31);
    suite_assert((unsigned)(*((vu32*)0x3023FFC) != SHWRAM_END1), 0x005, 32);
    suite_assert((unsigned)(*((vu32*)0x3024000) != SHWRAM_START2), 0x006, 33);
    suite_assert((unsigned)(*((vu32*)0x3027FFC) != SHWRAM_END2), 0x007, 34);

    // check ARM7 WRAMSTAT: should be 0
    suite_assert((unsigned)((ARM7_read_byte(0x4000241) & 3) != 0), 0x008, 37);

    // ARM7 should read mirrors of its own WRAM in this area
    // first, make it write start/end value to its own 64 KB WRAM
    ARM7_write_word(0x3800000, ARM7RAM_START);
    ARM7_write_word(0x380FFFC, ARM7RAM_END);

    // quickly check to make sure it was written to ARM7RAM correctly
    // should not be necessary as the ARM7 is running code from there
    // if the memory region isn't working, we would never have gotten this far
    suite_assert((unsigned)(ARM7_read_word(0x3800000) != ARM7RAM_START), 0x009, 47);
    suite_assert((unsigned)(ARM7_read_word(0x380FFFC) != ARM7RAM_END), 0x00A, 48);

    // now make ARM7 read from the shared WRAM region (03000000-037FFFFF)
    // should contain mirror of its own WRAM -> the magic values should be there
    suite_assert((unsigned)(ARM7_read_word(0x3000000) != ARM7RAM_START), 0x00B, 52);
    suite_assert((unsigned)(ARM7_read_word(0x300FFFC) != ARM7RAM_END), 0x00C, 53);
    // + read from mirror
    suite_assert((unsigned)(ARM7_read_word(0x3400000) != ARM7RAM_START), 0x00D, 55);
    suite_assert((unsigned)(ARM7_read_word(0x340FFFC) != ARM7RAM_END), 0x00E, 56);


    // switch to mode 1: we get second half, ARM7 gets first half
    REG_WRAMCNT = 1;

    // check WRAMSTAT
    suite_assert((unsigned)((ARM7_read_byte(0x4000241) & 3) != 1), 0x00F, 63);

    // make sure we can read the second half anywhere
    suite_assert((unsigned)(*((vu32*)0x3000000) != SHWRAM_START2), 0x010, 66);
    suite_assert((unsigned)(*((vu32*)0x3003FFC) != SHWRAM_END2), 0x011, 67);
    suite_assert((unsigned)(*((vu32*)0x304C000) != SHWRAM_START2), 0x012, 68);
    suite_assert((unsigned)(*((vu32*)0x304FFFC) != SHWRAM_END2), 0x013, 69);

    // and ARM7 should be able to read the first half
    suite_assert((unsigned)(ARM7_read_word(0x3000000) != SHWRAM_START1), 0x014, 72);
    suite_assert((unsigned)(ARM7_read_word(0x3003FFC) != SHWRAM_END1), 0x015, 73);
    suite_assert((unsigned)(ARM7_read_word(0x300C000) != SHWRAM_START1), 0x016, 74);
    suite_assert((unsigned)(ARM7_read_word(0x300FFFC) != SHWRAM_END1), 0x017, 75);


    // switch to mode 2: we get first half, ARM7 gets second half
    REG_WRAMCNT = 2;

    // check WRAMSTAT
    suite_assert((unsigned)((ARM7_read_byte(0x4000241) & 3) != 2), 0x018, 82);

    // make sure we can read the first half anywhere
    suite_assert((unsigned)(*((vu32*)0x3000000) != SHWRAM_START1), 0x019, 85);
    suite_assert((unsigned)(*((vu32*)0x3003FFC) != SHWRAM_END1), 0x01A, 86);
    suite_assert((unsigned)(*((vu32*)0x304C000) != SHWRAM_START1), 0x01B, 87);
    suite_assert((unsigned)(*((vu32*)0x304FFFC) != SHWRAM_END1), 0x01C, 88);

    // and ARM7 should be able to read the second half
    suite_assert((unsigned)(ARM7_read_word(0x3000000) != SHWRAM_START2), 0x01D, 91);
    suite_assert((unsigned)(ARM7_read_word(0x3003FFC) != SHWRAM_END2), 0x01E, 92);
    suite_assert((unsigned)(ARM7_read_word(0x300C000) != SHWRAM_START2), 0x01F, 93);
    suite_assert((unsigned)(ARM7_read_word(0x300FFFC) != SHWRAM_END2), 0x020, 94);


    // switch to mod 3: ARM7 gets everything
    REG_WRAMCNT = 3;

    // check WRAMSTAT
    suite_assert((unsigned)((ARM7_read_byte(0x4000241) & 3) != 3), 0x021, 101);

    // ensure ARM7 can read it from anywhere
    suite_assert((unsigned)(ARM7_read_word(0x3000000) != SHWRAM_START1), 0x022, 104);
    suite_assert((unsigned)(ARM7_read_word(0x3003FFC) != SHWRAM_END1), 0x023, 105);
    suite_assert((unsigned)(ARM7_read_word(0x300C000) != SHWRAM_START2), 0x024, 106);
    suite_assert((unsigned)(ARM7_read_word(0x300FFFC) != SHWRAM_END2), 0x025, 107);
    // + from a mirror
    suite_assert((unsigned)(ARM7_read_word(0x3110000) != SHWRAM_START1), 0x026, 109);
    suite_assert((unsigned)(ARM7_read_word(0x3223FFC) != SHWRAM_END1), 0x027, 110);
    suite_assert((unsigned)(ARM7_read_word(0x333C000) != SHWRAM_START2), 0x028, 111);
    suite_assert((unsigned)(ARM7_read_word(0x344FFFC) != SHWRAM_END2), 0x029, 112);


    // tell ARM7 we're done
    ARM7_end_rw();
}
