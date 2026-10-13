# vbagx → NooDS-Wii JIT Architecture Mapping (`evidence/vbagx_arch_map.md`)

Reference study commit: `vbagx` `4d5b9984e9b7457c9146e37dace5b95fa04b73ef`
Target repository: `NooDS-Wii` base `1c995b48c37ebf3645646968c416958f79264137` (`feature/arm-ppc-jit`)

## 1. Architectural Differences: vbagx vs. NooDS-Wii

| Dimension | `vbagx` (`4d5b9984`) | `NooDS-Wii` (`1c995b4`) | NooDS-Wii JIT Requirement |
|---|---|---|---|
| **Guest CPUs** | Single CPU (GBA ARM7TDMI) | Dual CPU (`core.interpreter[0]` = ARM946E-S ARMv5TE, `core.interpreter[1]` = ARM7TDMI ARMv4T, `NooDS-Wii/core.h:101`) + GBA mode on `interpreter[1]` (`core.gbaMode`) | Partition JIT state per CPU (`arm7 == false` for ARM9, `arm7 == true` for ARM7) or include CPU bit in all cache keys and tables. |
| **Instruction Sets** | Thumb-only (16-bit) (`JIT.h:11-18`) | Both ARM (32-bit) and Thumb (16-bit) on both CPUs (`cpsr & BIT(5)` in `NooDS-Wii/interpreter.h:56`, `interpreter.cpp:230-247`) | Support both ARM and Thumb modes in Phase 1 (1:1 fallback stubs) and Phase 2 (blocks/traces). |
| **Register File** | Flat `reg[0..15].I` array (`GBA-thumb.cpp:1411`) | Banked pointer table `uint32_t *registers[32]` pointing into `registersUsr[16]`, `registersFiq[7]`, `registersSvc[2]`, `registersAbt[2]`, `registersIrq[2]`, `registersUnd[2]` (`NooDS-Wii/interpreter.h:77-86`, `interpreter.cpp:333-408`) | Access guest registers through `Interpreter::registers[i]` pointers (or reload on mode changes via `setCpsr`/`swapRegisters`); derive member offsets dynamically from a live `Interpreter` instance because `Interpreter` mixes `public:` and `private:` sections (`interpreter.h:30,70`). |
| **Flags & Status** | Unpacked 4-word `gbaFlags` array (`JITCompiler.cpp:158-166`) | Packed in `uint32_t cpsr` (bits 31..28 = N,Z,C,V; bit 27 = sticky Q on ARM9; bit 7 = I; bit 6 = F; bit 5 = T; bits 4..0 = mode) (`NooDS-Wii/interpreter.h:85`, `interpreter_alu.cpp:65`) | Preserve exact `cpsr` bit layout: N=31, Z=30, C=29, V=28, Q=27 (ARM9), I=7, F=6, T=5, mode=4..0. Never clobber Q or control bits when updating NZCV. |
| **Pipeline & PC Fetch** | `cpuPrefetch[0..1]` + `armNextPC`, `reg[15].I = pc + 4` (`GBA-thumb.cpp:1405,1421-1427`) | 2-stage `pipeline[0..1]`, visible `*registers[15]` is instruction address + 4 (Thumb) or + 8 (ARM) at handler execution (`NooDS-Wii/interpreter.cpp:224-248,320-331`), plus fast-fetch host pointer `uint8_t *pcData` into `readMap7`/`readMap9A` (`interpreter.h:74`, `interpreter.cpp:250-264`) | Never desynchronize `pipeline[0..1]`, `*registers[15]`, or `pcData`. Any branch or multi-instruction block exit that changes PC without running `flushPipeline()` must call `flushPipeline()` (or keep `pipeline`/`pcData` exact). |
| **Reserved Condition (`0xF`)** | Not applicable (Thumb-only) | `condition[((opcode >> 24) & 0xF0) | (cpsr >> 28)] == 2` calls `handleReserved(opcode)` (`NooDS-Wii/interpreter.cpp:242-245,424-449`): ARM9 `BLX label`, HLE BIOS IRQ return (`0xFF000000`), and DLDI HLE opcodes (`DLDI_START..DLDI_STOP`) | Never blanket-ignore condition nibble `0xF`; route `case 2` through `handleReserved(opcode)` or bail to the interpreter before executing `0xF`-condition opcodes. |
| **Coprocessor (CP15)** | None | ARM9 CP15 (`mrc`/`mcr` in `NooDS-Wii/interpreter_transfer.cpp:1174,1185` → `core->cp15.read`/`write`, which can remap ITCM/DTCM via `memory.updateMap9`) | CP15 instructions always exit/fallback to the interpreter (`AGENTS.md` §5 G6, §7, §8). |
| **Code Arena Placement** | MEM1 via custom `wii_mem.ld` (`Makefile.wii:53-59`) | Standard `rvl.ld` in MEM1; 64 MiB MEM2 managed via `Noods_MEM2_Alloc()` (`NooDS-Wii/main.cpp:63-78`) | Allocate JIT code pools and block metadata from MEM2 via `Noods_MEM2_Alloc()` (`AGENTS.md` §8.1). Use `mtctr`/`bctr` (`bctrl`) when crossing the MEM1 (`0x80xxxxxx`) ↔ MEM2 (`0x90xxxxxx`) >32 MiB boundary. |

## 2. Cache Identity and Partitioning

- Each CPU (`arm9` index `0`, `arm7` index `1`) has its own cache partition (or CPU-tagged entries) so ARM9 and ARM7 blocks never collide or cross-execute.
- Cache tags **must** include:
  1. Guest instruction address `pc` (or `pc` + physical/mapped host page pointer `pcData` page to handle TCM/WRAM/VRAM bank remapping safely).
  2. ISA mode (`isThumb = (cpsr >> 5) & 1`).
  3. CPU identifier (`arm7 = 0` for ARM9, `arm7 = 1` for ARM7) and `gbaMode`.
  4. In Phase 1 (and as a fast guard in Phase 2), the fetched initial opcode (`opcode`) and non-zero cache `epoch`.

## 3. Audit of All Guest Memory Write Paths & SMC Invalidation

We audited every memory write in `NooDS-Wii/*.cpp`:

1. **`Memory::write<T>(bool arm7, uint32_t address, T value, bool tcm)` (`NooDS-Wii/memory.h:152-164`):**
   - Fast path writes through `writeMap = arm7 ? writeMap7 : (tcm ? writeMap9A : writeMap9B)` (`memory.h:154-160`).
   - Slow path calls `Memory::writeFallback<T>(arm7, address, value)` (`memory.cpp:721-820`), which handles I/O (`0x4000000`), Palettes (`0x5000000`), multi-mapped VRAM (`0x6000000`), OAM (`0x7000000`), and Cartridge SRAM/EEPROM.
2. **CPU Stores (`NooDS-Wii/interpreter_transfer.cpp:177-1460`):**
   - Every ARM and Thumb store (`str`, `strb`, `strh`, `strd`, `stm`, `swp`, `swpb`) calls `core->memory.write<T>(arm7, addr, val)`.
3. **DMA Transfers (`NooDS-Wii/dma.cpp:57,72,97`):**
   - Both ARM9 (`dma[0]`) and ARM7/GBA (`dma[1]`) channels write exclusively via `core->memory.write<uint32_t/uint16_t>(cpu, dstAddrs[channel], value, false)`.
4. **DLDI HLE Sector Reads (`NooDS-Wii/dldi.cpp:105`):**
   - `Dldi::readSectors` writes sector bytes to guest RAM via `core->memory.write<uint8_t>(arm7, buf + i, data[i])`.
   - `Dldi::patchRom` (`dldi.cpp:30-79`) runs at cartridge load before CPU execution starts.
5. **HLE BIOS & HLE ARM7 (`NooDS-Wii/hle_bios.cpp:103-577`, `NooDS-Wii/hle_arm7.cpp:54-95`):**
   - SWI decompression/copy routines (`CpuSet`, `CpuFastSet`, `LZ77UnComp*`, `HuffUnComp`, `RLUnComp*`, `BitUnPack`) and HLE ARM7 IPC updates all write via `core->memory.write<T>(...)`.
6. **GPU Display Capture, SPI Firmware, SPU Sound Capture, WiFi (`NooDS-Wii/gpu.cpp:339-381`, `spi.cpp:154`, `spu.cpp:765-770`, `wifi.cpp:225-505`):**
   - All write via `core->memory.write<T>(...)`.
7. **Memory Remapping (`NooDS-Wii/memory.cpp:126-128,178-350,603-605`):**
   - `Memory::updateMap9`, `Memory::updateMap7`, and `Memory::loadState` change how guest virtual addresses map to backing RAM/WRAM/ITCM/DTCM/VRAM.

### SMC Invalidation Strategy for NooDS-Wii

- Because **all** runtime guest stores (CPU, DMA, DLDI, HLE BIOS, SPU/GPU capture) funnel through `Memory::write<T>` (and remaps go through `updateMap9`/`updateMap7`), hooking `Memory::write<T>` and `updateMap9`/`updateMap7` catches every guest code modification across both CPUs.
- Crucially, ARM9 and ARM7 **share** Main RAM (`0x02000000`), Shared WRAM (`0x03000000`), and ARM7-mapped VRAM (`0x06000000`). A write by ARM9, ARM7, or DMA to a shared region must invalidate compiled blocks on **both** ARM9 and ARM7 caches!
- Tracking SMC by physical host page/region or invalidating both CPUs' page registries on writes to executable regions (`0x00000000..0x07FFFFFF`, including ITCM `0x00000000..0x01FFFFFF`, Main RAM `0x02xxxxxx`, WRAM `0x03xxxxxx`, VRAM `0x06xxxxxx`) guarantees cross-CPU and DMA coherency.

## 4. Non-Goals (Explicitly Excluded from Adaptation)

- Do **not** copy any `vbagx` source files (`JIT.h`, `JITCache.*`, `JITCompiler.cpp`, `JITPPCEmitter.h`, `JITTrampoline.S`). All NooDS-Wii JIT code is written cleanly from scratch for NooDS-Wii's `Interpreter` and `Core` classes under namespace `JitPpc`.
- Do **not** port GameCube ARAM paging, Wii U WUT codegen toggles, GBA PPU/APU components, or `vbagx`'s MEM1 linker script (`wii_mem.ld`).
