#include <cstring>
#include "core.h"

// ── Macro helpers ────────────────────────────────────────────────────────────

#define DEF_IO_8(addr, func) \
    case addr: \
        base -= addr; size = 1; func; break;

#define DEF_IO16(addr, func) \
    case addr: case addr + 1: \
        base -= addr; size = 2; func; break;

#define DEF_IO32(addr, func) \
    case addr + 0: case addr + 1: \
    case addr + 2: case addr + 3: \
        base -= addr; size = 4; func; break;

#define IOWR_PARAMS8  data << (base * 8)
#define IOWR_PARAMS   mask << (base * 8), data << (base * 8)

// ── Explicit template instantiations ─────────────────────────────────────────

template uint8_t  Memory::read<uint8_t> (bool, uint32_t, bool);
template uint16_t Memory::read<uint16_t>(bool, uint32_t, bool);
template uint32_t Memory::read<uint32_t>(bool, uint32_t, bool);
template void Memory::write<uint8_t> (bool, uint32_t, uint8_t,  bool);
template void Memory::write<uint16_t>(bool, uint32_t, uint16_t, bool);
template void Memory::write<uint32_t>(bool, uint32_t, uint32_t, bool);

// ── VramMapping (specializations now in header; keep .cpp for non-inline) ───

// saveState / loadState — unchanged, use fwrite/fread bulk
void Memory::saveState(FILE *file) {
    fwrite(ram,       1, core->dsiMode ? 0x1000000 : 0x400000, file);
    fwrite(wram,      1, sizeof(wram),      file);
    fwrite(instrTcm,  1, sizeof(instrTcm),  file);
    fwrite(dataTcm,   1, sizeof(dataTcm),   file);
    fwrite(wram7,     1, sizeof(wram7),     file);
    fwrite(wifiRam,   1, sizeof(wifiRam),   file);
    fwrite(palette,   1, sizeof(palette),   file);
    fwrite(vramA,     1, sizeof(vramA),     file);
    fwrite(vramB,     1, sizeof(vramB),     file);
    fwrite(vramC,     1, sizeof(vramC),     file);
    fwrite(vramD,     1, sizeof(vramD),     file);
    fwrite(vramE,     1, sizeof(vramE),     file);
    fwrite(vramF,     1, sizeof(vramF),     file);
    fwrite(vramG,     1, sizeof(vramG),     file);
    fwrite(vramH,     1, sizeof(vramH),     file);
    fwrite(vramI,     1, sizeof(vramI),     file);
    fwrite(oam,       1, sizeof(oam),       file);
    fwrite(&gbaBiosAddr, sizeof(gbaBiosAddr), 1, file);
    fwrite(dmaFill,   4, sizeof(dmaFill)/4, file);
    fwrite(vramCnt,   1, sizeof(vramCnt),   file);
    fwrite(&wramCnt,  sizeof(wramCnt),  1,  file);
    fwrite(&haltCnt,  sizeof(haltCnt),  1,  file);
}

void Memory::loadState(FILE *file) {
    fread(ram,       1, core->dsiMode ? 0x1000000 : 0x400000, file);
    fread(wram,      1, sizeof(wram),      file);
    fread(instrTcm,  1, sizeof(instrTcm),  file);
    fread(dataTcm,   1, sizeof(dataTcm),   file);
    fread(wram7,     1, sizeof(wram7),     file);
    fread(wifiRam,   1, sizeof(wifiRam),   file);
    fread(palette,   1, sizeof(palette),   file);
    fread(vramA,     1, sizeof(vramA),     file);
    fread(vramB,     1, sizeof(vramB),     file);
    fread(vramC,     1, sizeof(vramC),     file);
    fread(vramD,     1, sizeof(vramD),     file);
    fread(vramE,     1, sizeof(vramE),     file);
    fread(vramF,     1, sizeof(vramF),     file);
    fread(vramG,     1, sizeof(vramG),     file);
    fread(vramH,     1, sizeof(vramH),     file);
    fread(vramI,     1, sizeof(vramI),     file);
    fread(oam,       1, sizeof(oam),       file);
    fread(&gbaBiosAddr, sizeof(gbaBiosAddr), 1, file);
    fread(dmaFill,   4, sizeof(dmaFill)/4, file);
    fread(vramCnt,   1, sizeof(vramCnt),   file);
    fread(&wramCnt,  sizeof(wramCnt),  1,  file);
    fread(&haltCnt,  sizeof(haltCnt),  1,  file);
    updateMap9(0x00000000, 0xFFFFFFFF);
    updateMap7(0x00000000, 0xFFFFFFFF);
    updateVram();
}

// BIOS loading — unchanged
bool Memory::loadBios9() {
    if (FILE *file = fopen(Settings::bios9Path.c_str(), "rb")) {
        fread(bios9, 1, 0x1000, file);
        fclose(file);
        return true;
    }
    bios9[3] = 0xFF;
    core->interpreter[0].bios = &core->hleBios[0];
    return false;
}
bool Memory::loadBios7() {
    if (FILE *file = fopen(Settings::bios7Path.c_str(), "rb")) {
        fread(bios7, 1, 0x4000, file);
        fclose(file);
        return true;
    }
    bios7[3] = 0xFF;
    core->interpreter[1].bios = &core->hleBios[1];
    return false;
}
bool Memory::loadGbaBios() {
    if (FILE *file = fopen(Settings::gbaBiosPath.c_str(), "rb")) {
        fread(gbaBios, 1, 0x4000, file);
        fclose(file);
        return true;
    }
    gbaBios[3] = 0xFF;
    return false;
}
void Memory::copyBiosLogo(uint8_t *logo) {
    if (bios9[3] == 0xFF)
        memcpy(&bios9[0x20], logo, 0x9C);
}

// ── updateMap9 — cache-friendly rewrite ──────────────────────────────────────

void Memory::updateMap9(uint32_t start, uint32_t end, bool tcm) {
    const bool dsiMode    = core->dsiMode;
    const uint32_t ramMask = dsiMode ? 0xFFFFFFu : 0x3FFFFFu;

    for (uint64_t address = start; address < end; address += 0x1000) {
        uint8_t *&read  = (tcm ? readMap9A  : readMap9B )[address >> 12];
        uint8_t *&write = (tcm ? writeMap9A : writeMap9B)[address >> 12];
        read = write = nullptr;

        switch (address & 0xFF000000) {
        case 0xC000000:
            if (!dsiMode) break;
            // fall through
        case 0x2000000:
            read = write = &ram[address & ramMask];
            break;

        case 0x3000000:
            switch (wramCnt) {
                case 0: read = write = &wram[address & 0x7FFF]; break;
                case 1: read = write = &wram[(address & 0x3FFF) + 0x4000]; break;
                case 2: read = write = &wram[address & 0x3FFF]; break;
            }
            break;

        case 0x6000000: {
            VramMapping *m = getVramMapping9(address);
            if (m->count == 1)
                read = write = &m->mappings[0][address & 0x3FFF];
            break;
        }

        case 0x8000000: case 0x9000000:
            read = core->cartridgeGba.getRom(address);
            break;

        case 0xFF000000:
            if ((address & 0xFFFF8000) == 0xFFFF0000)
                read = &bios9[address & 0xFFFF];
            break;
        }

        if (!tcm) continue;

        // TCM overlay — checked after main map
        if (address < core->cp15.itcmSize) {
            if (core->cp15.itcmCanRead)  read  = &instrTcm[address & 0x7FFF];
            if (core->cp15.itcmCanWrite) write = &instrTcm[address & 0x7FFF];
        } else {
            uint32_t dtcmOff = address - core->cp15.dtcmAddr;
            if (dtcmOff < core->cp15.dtcmSize) {
                if (core->cp15.dtcmCanRead)  read  = &dataTcm[dtcmOff & 0x3FFF];
                if (core->cp15.dtcmCanWrite) write = &dataTcm[dtcmOff & 0x3FFF];
            }
        }
    }

    if (!tcm) updateMap9(start, end, true);
    core->interpreter[0].getOpcode16();
}

// ── updateMap7 — cache-friendly rewrite ──────────────────────────────────────

void Memory::updateMap7(uint32_t start, uint32_t end) {
    const bool dsiMode    = core->dsiMode;
    const bool gbaMode    = core->gbaMode;
    const uint32_t ramMask = dsiMode ? 0xFFFFFFu : 0x3FFFFFu;

    for (uint64_t address = start; address < end; address += 0x1000) {
        uint8_t *&read  = readMap7 [address >> 12];
        uint8_t *&write = writeMap7[address >> 12];
        read = write = nullptr;

        if (gbaMode) {
            switch (address & 0xFF000000) {
            case 0x2000000:
                read = write = &ram[address & 0x3FFFFu];
                break;
            case 0x3000000:
                read = write = &wram[address & 0x7FFF];
                break;
            case 0x6000000:
                read = write = &vramC[address & ((address & 0x10000) ? 0x17FFF : 0xFFFF)];
                break;
            case 0x8000000: case 0x9000000: case 0xA000000:
            case 0xB000000: case 0xC000000:
                if (address > 0x8000000 || !core->rtc.readGpControl())
                    read = core->cartridgeGba.getRom(address);
                break;
            }
        } else {
            switch (address & 0xFF000000) {
            case 0x0000000:
                if (address < 0x4000) read = &bios7[address];
                break;
            case 0xC000000:
                if (!dsiMode) break;
                // fall through
            case 0x2000000:
                read = write = &ram[address & ramMask];
                break;
            case 0x3000000:
                if (!(address & 0x800000)) {
                    switch (wramCnt) {
                        case 1: read = write = &wram[address & 0x3FFF]; break;
                        case 2: read = write = &wram[(address & 0x3FFF) + 0x4000]; break;
                        case 3: read = write = &wram[address & 0x7FFF]; break;
                    }
                }
                if (!read) read = write = &wram7[address & 0xFFFF];
                break;
            case 0x4000000:
                if (address & 0x800000) {
                    uint32_t addr = address & ~0x8000u;
                    if (addr >= 0x4804000 && addr < 0x4806000)
                        read = write = &wifiRam[addr & 0x1FFF];
                }
                break;
            case 0x6000000: {
                VramMapping *m = &vram7[(address & 0x3FFFFu) >> 17];
                if (m->count == 1)
                    read = write = &m->mappings[0][address & 0x1FFFFu];
                break;
            }
            case 0x8000000: case 0x9000000:
                read = core->cartridgeGba.getRom(address);
                break;
            }
        }
    }

    core->interpreter[1].getOpcode16();
}

// ── updateVram — unchanged logic, minor style cleanup ────────────────────────

void Memory::updateVram() {
    memset(engABg,    0, sizeof(engABg));
    memset(engBBg,    0, sizeof(engBBg));
    memset(engAObj,   0, sizeof(engAObj));
    memset(engBObj,   0, sizeof(engBObj));
    memset(lcdc,      0, sizeof(lcdc));
    memset(vram7,     0, sizeof(vram7));
    memset(engAExtPal,0, sizeof(engAExtPal));
    memset(engBExtPal,0, sizeof(engBExtPal));
    memset(tex3D,     0, sizeof(tex3D));
    memset(pal3D,     0, sizeof(pal3D));
    vramStat = 0;

    // Block A
    if (vramCnt[0] & 0x80) {
        uint8_t ofs = (vramCnt[0] >> 3) & 0x3;
        switch (vramCnt[0] & 0x7) {
        case 0: for (int i=0;i<8;i++) lcdc[i].add(&vramA[i<<14]); break;
        case 1: for (int i=0;i<8;i++) engABg[(ofs<<3)+i].add(&vramA[i<<14]); break;
        case 2: for (int i=0;i<8;i++) engAObj[(ofs<<3)+i].add(&vramA[i<<14]); break;
        case 3: tex3D[ofs] = &vramA[0]; break;
        }
    }
    // Block B
    if (vramCnt[1] & 0x80) {
        uint8_t ofs = (vramCnt[1] >> 3) & 0x3;
        switch (vramCnt[1] & 0x7) {
        case 0: for (int i=0;i<8;i++) lcdc[8+i].add(&vramB[i<<14]); break;
        case 1: for (int i=0;i<8;i++) engABg[(ofs<<3)+i].add(&vramB[i<<14]); break;
        case 2: for (int i=0;i<8;i++) engAObj[(ofs<<3)+i].add(&vramB[i<<14]); break;
        case 3: tex3D[ofs] = &vramB[0]; break;
        }
    }
    // Block C
    if (vramCnt[2] & 0x80) {
        uint8_t ofs = (vramCnt[2] >> 3) & 0x3;
        switch (vramCnt[2] & 0x7) {
        case 0: for (int i=0;i<8;i++) lcdc[16+i].add(&vramC[i<<14]); break;
        case 1: for (int i=0;i<8;i++) engABg[(ofs<<3)+i].add(&vramC[i<<14]); break;
        case 2: vram7[ofs & 1].add(&vramC[0]); vramStat |= 1; break;
        case 3: tex3D[ofs] = &vramC[0]; break;
        case 4: for (int i=0;i<8;i++) engBBg[i].add(&vramC[i<<14]); break;
        }
    }
    // Block D
    if (vramCnt[3] & 0x80) {
        uint8_t ofs = (vramCnt[3] >> 3) & 0x3;
        switch (vramCnt[3] & 0x7) {
        case 0: for (int i=0;i<8;i++) lcdc[24+i].add(&vramD[i<<14]); break;
        case 1: for (int i=0;i<8;i++) engABg[(ofs<<3)+i].add(&vramD[i<<14]); break;
        case 2: vram7[ofs & 1].add(&vramD[0]); vramStat |= 2; break;
        case 3: tex3D[ofs] = &vramD[0]; break;
        case 4: for (int i=0;i<8;i++) engBObj[i].add(&vramD[i<<14]); break;
        }
    }
    // Block E
    if (vramCnt[4] & 0x80) {
        switch (vramCnt[4] & 0x7) {
        case 0: for (int i=0;i<4;i++) lcdc[32+i].add(&vramE[i<<14]); break;
        case 1: for (int i=0;i<4;i++) engABg[i].add(&vramE[i<<14]); break;
        case 2: for (int i=0;i<4;i++) engAObj[i].add(&vramE[i<<14]); break;
        case 3: for (int i=0;i<4;i++) pal3D[i] = &vramE[i<<14]; break;
        case 4: for (int i=0;i<4;i++) engAExtPal[i] = &vramE[i<<13]; break;
        }
    }
    // Block F
    if (vramCnt[5] & 0x80) {
        uint8_t ofs = (vramCnt[5] >> 3) & 0x3;
        switch (vramCnt[5] & 0x7) {
        case 0: lcdc[36].add(&vramF[0]); break;
        case 1: for (int i=0;i<2;i++) engABg[((ofs&2)<<1)+(ofs&1)+(i<<1)].add(&vramF[0]); break;
        case 2: for (int i=0;i<2;i++) engAObj[((ofs&2)<<1)+(ofs&1)+(i<<1)].add(&vramF[0]); break;
        case 3: pal3D[((ofs&2)<<1)+(ofs&1)] = &vramF[0]; break;
        case 4: for (int i=0;i<2;i++) engAExtPal[((ofs&1)<<1)+i] = &vramF[i<<13]; break;
        case 5: engAExtPal[4] = &vramF[0]; break;
        }
    }
    // Block G
    if (vramCnt[6] & 0x80) {
        uint8_t ofs = (vramCnt[6] >> 3) & 0x3;
        switch (vramCnt[6] & 0x7) {
        case 0: lcdc[37].add(&vramG[0]); break;
        case 1: for (int i=0;i<2;i++) engABg[((ofs&2)<<1)+(ofs&1)+(i<<1)].add(&vramG[0]); break;
        case 2: for (int i=0;i<2;i++) engAObj[((ofs&2)<<1)+(ofs&1)+(i<<1)].add(&vramG[0]); break;
        case 3: pal3D[((ofs&2)<<1)+(ofs&1)] = &vramG[0]; break;
        case 4: for (int i=0;i<2;i++) engAExtPal[((ofs&1)<<1)+i] = &vramG[i<<13]; break;
        case 5: engAExtPal[4] = &vramG[0]; break;
        }
    }
    // Block H
    if (vramCnt[7] & 0x80) {
        switch (vramCnt[7] & 0x7) {
        case 0: for (int i=0;i<2;i++) lcdc[38+i].add(&vramH[i<<14]); break;
        case 1:
            for (int i=0;i<2;i++) {
                engBBg[0+i].add(&vramH[i<<14]);
                engBBg[4+i].add(&vramH[i<<14]);
            }
            break;
        case 2: for (int i=0;i<4;i++) engBExtPal[i] = &vramH[i<<13]; break;
        }
    }
    // Block I
    if (vramCnt[8] & 0x80) {
        switch (vramCnt[8] & 0x7) {
        case 0: lcdc[40].add(&vramI[0]); break;
        case 1:
            for (int i=0;i<2;i++) {
                engBBg[2+i].add(&vramI[0]);
                engBBg[6+i].add(&vramI[0]);
            }
            break;
        case 2: for (int i=0;i<8;i++) engBObj[i].add(&vramI[0]); break;
        case 3: engBExtPal[4] = &vramI[0]; break;
        }
    }

    updateMap9(0x6000000, 0x7000000);
    updateMap7(0x6000000, 0x7000000);
    core->gpu.invalidate3D();
}

// ── readFallback — optimized helper, uses readLE ──────────────────────────────

template <typename T> T Memory::readFallback(bool arm7, uint32_t address) {
    address &= ~(uint32_t)(sizeof(T) - 1);
    const uint8_t *data = nullptr;

    if (UNLIKELY(!arm7)) { // ARM9
        switch (address & 0xFF000000) {
        case 0x4000000: return ioRead9<T>(address);
        case 0x5000000: data = &palette[address & 0x7FF]; break;
        case 0x6000000: {
            VramMapping *m = getVramMapping9(address);
            if (m->count == 0) break;
            return m->read<T>(address & 0x3FFF);
        }
        case 0x7000000: data = &oam[address & 0x7FF]; break;
        case 0x8000000: case 0x9000000: return (T)0xFFFFFFFF;
        case 0xA000000: return core->cartridgeGba.sramRead(address + 0x4000000);
        }
    } else if (core->gbaMode) {
        switch (address & 0xFF000000) {
        case 0x0000000:
            if (address < 0x4000)
                data = &gbaBios[(core->interpreter[1].getPC() < 0x4000)
                                ? (gbaBiosAddr = address) : gbaBiosAddr];
            break;
        case 0x4000000: return ioReadGba<T>(address);
        case 0x5000000: data = &palette[address & 0x3FF]; break;
        case 0x7000000: data = &oam[address & 0x3FF]; break;
        case 0xD000000:
            if (core->cartridgeGba.isEeprom(address))
                return core->cartridgeGba.eepromRead();
            // fall through
        case 0x8000000: case 0x9000000: case 0xA000000:
        case 0xB000000: case 0xC000000:
            if (address >= 0x80000C4 && address < 0x80000CA)
                return ioReadGba<T>(address);
            if ((data = core->cartridgeGba.getRom(address)))
                break;
            return (T)0xFFFFFFFF;
        case 0xE000000: return core->cartridgeGba.sramRead(address);
        }
    } else { // ARM7
        switch (address & 0xFF000000) {
        case 0x4000000: return ioRead7<T>(address);
        case 0x6000000: {
            VramMapping *m = &vram7[(address & 0x3FFFFu) >> 17];
            if (m->count == 0) break;
            return m->read<T>(address & 0x1FFFFu);
        }
        case 0x8000000: case 0x9000000: return (T)0xFFFFFFFF;
        case 0xA000000: return core->cartridgeGba.sramRead(address + 0x4000000);
        }
    }

    if (data) return readLE<T>(data);

    if (!core->gbaMode) {
        LOG_WARN("Unmapped ARM%d memory read: 0x%X\n", arm7 ? 7 : 9, address);
        return 0;
    }
    LOG_WARN("Unmapped GBA memory read: 0x%X\n", address);
    if (address == core->interpreter[1].getPC()) return 0;
    return read<T>(arm7, core->interpreter[1].getPC());
}

template <typename T> void Memory::writeFallback(bool arm7, uint32_t address, T value) {
    address &= ~(uint32_t)(sizeof(T) - 1);
    uint8_t *data = nullptr;

    if (UNLIKELY(!arm7)) {
        switch (address & 0xFF000000) {
        case 0x4000000: ioWrite9<T>(address, value); return;
        case 0x5000000: data = &palette[address & 0x7FF]; break;
        case 0x6000000: {
            VramMapping *m = getVramMapping9(address);
            if (m->count == 0) break;
            m->write<T>(address & 0x3FFF, value);
            return;
        }
        case 0x7000000: data = &oam[address & 0x7FF]; break;
        case 0xA000000: core->cartridgeGba.sramWrite(address + 0x4000000, value); return;
        }
    } else if (core->gbaMode) {
        switch (address & 0xFF000000) {
        case 0x4000000: ioWriteGba<T>(address, value); return;
        case 0x5000000: data = &palette[address & 0x3FF]; break;
        case 0x7000000: data = &oam[address & 0x3FF]; break;
        case 0x8000000:
            if (address >= 0x80000C4 && address < 0x80000CA) {
                ioWriteGba<T>(address, value); return;
            }
            break;
        case 0xD000000:
            if (core->cartridgeGba.isEeprom(address)) {
                core->cartridgeGba.eepromWrite(value); return;
            }
            break;
        case 0xE000000: core->cartridgeGba.sramWrite(address, value); return;
        }
    } else {
        switch (address & 0xFF000000) {
        case 0x4000000: ioWrite7<T>(address, value); return;
        case 0x6000000: {
            VramMapping *m = &vram7[(address & 0x3FFFFu) >> 17];
            if (m->count == 0) break;
            m->write<T>(address & 0x1FFFFu, value);
            return;
        }
        case 0xA000000: core->cartridgeGba.sramWrite(address + 0x4000000, value); return;
        }
    }

    if (data) { writeLE<T>(data, value); return; }

    if (!core->gbaMode)
        LOG_WARN("Unmapped ARM%d memory write: 0x%X\n", arm7 ? 7 : 9, address);
    else
        LOG_WARN("Unmapped GBA memory write: 0x%X\n", address);
}

// ── I/O handlers — unchanged logic, keep as-is ───────────────────────────────
// (ioRead9, ioRead7, ioReadGba, ioWrite9, ioWrite7, ioWriteGba)
// These are already switch-table based; the compiler will generate jump tables.
// No structural changes needed — paste the originals here unchanged.

// ── Register helpers ──────────────────────────────────────────────────────────

void Memory::writeDmaFill(int channel, uint32_t mask, uint32_t value) {
    dmaFill[channel] = (dmaFill[channel] & ~mask) | (value & mask);
}

void Memory::writeVramCnt(int index, uint8_t value) {
    static const uint8_t masks[] = { 0x9B,0x9B,0x9F,0x9F,0x87,0x9F,0x9F,0x83,0x83 };
    uint8_t masked = value & masks[index];
    if (masked == (vramCnt[index] & masks[index])) return;
    vramCnt[index] = masked;
    updateVram();
}

void Memory::writeWramCnt(uint8_t value) {
    wramCnt = value & 0x3;
    updateMap9(0x3000000, 0x4000000);
    updateMap7(0x3000000, 0x4000000);
}

void Memory::writeHaltCnt(uint8_t value) {
    haltCnt = value & 0xC0;
    switch (haltCnt >> 6) {
    case 1: core->enterGbaMode(); break;
    case 2: core->interpreter[1].halt(0); break;
    case 3: LOG_CRIT("Unhandled request for sleep mode\n"); break;
    }
}

void Memory::writeGbaHaltCnt(uint8_t value) {
    core->interpreter[1].halt(0);
    if (value & 0x80)
        LOG_CRIT("Unhandled request for stop mode\n");
}
