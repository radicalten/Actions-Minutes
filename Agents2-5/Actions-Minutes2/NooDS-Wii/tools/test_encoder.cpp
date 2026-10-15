#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "../NooDS-Wii/arm_jit.h"

struct TestCase {
    const char *name;
    const char *asmText;
    uint32_t encoded;
};

int main(int argc, char **argv) {
    using namespace JitPpc;
    const TestCase cases[] = {
        {"01_li",       "li 3, -128",               li(R3, -128)},
        {"02_lis",      "lis 4, 0x1234",            lis(R4, 0x1234)},
        {"03_addi",     "addi 5, 31, 64",           addi(R5, R31, 64)},
        {"04_addis",    "addis 6, 31, -2",          addis(R6, R31, -2)},
        {"05_addic",    "addic 3, 4, 16",           addic(R3, R4, 16)},
        {"06_addic_rc", "addic. 3, 4, -1",          addic_rc(R3, R4, -1)},
        {"07_subfic",   "subfic 3, 4, 0",           subfic(R3, R4, 0)},
        {"08_mulli",    "mulli 3, 4, 12",           mulli(R3, R4, 12)},
        {"09_ori",      "ori 4, 4, 0x5678",         ori(R4, R4, 0x5678)},
        {"10_oris",     "oris 5, 4, 0x00FF",        oris(R5, R4, 0x00FF)},
        {"11_xori",     "xori 6, 5, 0x1234",        xori(R6, R5, 0x1234)},
        {"12_xoris",    "xoris 6, 5, 0x8000",       xoris(R6, R5, 0x8000)},
        {"13_andi_rc",  "andi. 7, 6, 0x00FF",       andi_rc(R7, R6, 0x00FF)},
        {"14_andis_rc", "andis. 7, 6, 0xF000",      andis_rc(R7, R6, 0xF000)},
        {"15_nop",      "nop",                      nop()},
        {"16_cmpwi",    "cmpwi 0, 3, -1",           cmpwi(0, R3, -1)},
        {"17_cmplwi",   "cmplwi 0, 4, 255",         cmplwi(0, R4, 255)},
        {"18_cmpw",     "cmpw 0, 3, 4",             cmpw(0, R3, R4)},
        {"19_cmplw",    "cmplw 0, 5, 6",            cmplw(0, R5, R6)},
        {"20_lwz",      "lwz 3, 12(31)",            lwz(R3, R31, 12)},
        {"21_stw",      "stw 4, 16(31)",            stw(R4, R31, 16)},
        {"22_stwu",     "stwu 1, -32(1)",           stwu(R1, R1, -32)},
        {"23_lhz",      "lhz 5, 8(31)",             lhz(R5, R31, 8)},
        {"24_sth",      "sth 5, 10(31)",            sth(R5, R31, 10)},
        {"25_lbz",      "lbz 6, 3(31)",             lbz(R6, R31, 3)},
        {"26_stb",      "stb 6, 4(31)",             stb(R6, R31, 4)},
        {"27_lmw",      "lmw 28, 16(1)",            lmw(R28, R1, 16)},
        {"28_stmw",     "stmw 28, 16(1)",           stmw(R28, R1, 16)},
        {"29_lhbrx",    "lhbrx 3, 0, 4",            lhbrx(R3, 0, R4)},
        {"30_lwbrx",    "lwbrx 3, 0, 4",            lwbrx(R3, 0, R4)},
        {"31_add",      "add 3, 4, 5",              add(R3, R4, R5)},
        {"32_addc",     "addc 3, 4, 5",             addc(R3, R4, R5)},
        {"33_subf",     "subf 3, 5, 4",             subf(R3, R5, R4)},
        {"34_subfc",    "subfc 3, 5, 4",            subfc(R3, R5, R4)},
        {"35_and",      "and 3, 4, 5",              and_(R3, R4, R5)},
        {"36_andc",     "andc 3, 4, 5",             andc(R3, R4, R5)},
        {"37_or",       "or 3, 4, 5",               or_(R3, R4, R5)},
        {"38_xor",      "xor 3, 4, 5",              xor_(R3, R4, R5)},
        {"39_nor",      "nor 3, 4, 5",              nor(R3, R4, R5)},
        {"40_eqv",      "eqv 3, 4, 5",              eqv(R3, R4, R5)},
        {"41_srawi",    "srawi 3, 4, 7",            srawi(R3, R4, 7)},
        {"42_cntlzw",   "cntlzw 7, 3",              cntlzw(R7, R3)},
        {"43_rlwinm",   "rlwinm 3, 4, 8, 0, 23",    rlwinm(R3, R4, 8, 0, 23)},
        {"44_rlwimi",   "rlwimi 6, 7, 25, 1, 1",    rlwimi(R6, R7, 25, 1, 1)},
        {"45_mtctr",    "mtctr 12",                 mtctr(R12)},
        {"46_mfxer",    "mfxer 7",                  mfxer(R7)},
        {"47_bctrl",    "bctrl",                    bctrl()},
        {"48_blr",      "blr",                      blr()},
        {"49_dcbst",    "dcbst 0, 3",               dcbst(0, R3)},
        {"50_icbi",     "icbi 0, 3",                icbi(0, R3)},
        {"51_sync",     "sync",                     sync()},
        {"52_isync",    "isync",                    isync()},
    };

    const size_t n = sizeof(cases) / sizeof(cases[0]);
    if (argc > 1 && std::string(argv[1]) == "--asm") {
        std::printf("    .section .text\n    .globl _start\n_start:\n");
        for (size_t i = 0; i < n; i++) {
            std::printf("    %s\n", cases[i].asmText);
        }
        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        std::printf("%02zu %s 0x%08x %s\n", i + 1, cases[i].name, cases[i].encoded, cases[i].asmText);
    }
    return 0;
}
