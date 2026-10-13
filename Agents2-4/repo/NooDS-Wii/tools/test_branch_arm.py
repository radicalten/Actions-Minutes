#!/usr/bin/env python3
"""Exercise the actual execBranchArm C++ helper on a host compiler (no PPC required).

Extract just the helper from arm_jit.cpp so the test does not require Wii headers,
PPC assembly, a full Core object, or a ROM that happens to execute BLX Rm.
The mocked flushPipeline checks branch state but does not validate the guest bus.
"""
import pathlib
import subprocess
import tempfile

source = pathlib.Path(__file__).resolve().parent.parent / "NooDS-Wii" / "arm_jit.cpp"
text = source.read_text()

def function(signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[start:end + 1]
    raise AssertionError("Unterminated function: " + signature)

branch = function("int ArmPpcJit::execBranchArm(").replace("ArmPpcJit::execBranchArm", "execBranchArm", 1)
classify = function("static inline bool isTerminatorBranchArm(")
cpp = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
uint32_t U8TO32(const uint8_t *p, int) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
struct Interpreter {
    static uint8_t condition[256];
    bool arm7 = false, flushed = false;
    uint32_t registersUsr[16] = {}, cpsr = 0, pipeline[2] = {};
    uint32_t *registers[16] = {};
    uint8_t *pcData = nullptr;
    explicit Interpreter(bool a) : arm7(a) {
        for (unsigned i = 0; i < 16; ++i) registers[i] = &registersUsr[i];
    }
    void flushPipeline() { flushed = true; }
    int handleReserved(uint32_t) { return 1; }
};
uint8_t Interpreter::condition[256] = {};
''' + classify + "\n" + branch + r'''
int main() {
    const uint32_t blxLR = 0xE12FFF3E, blxR3 = 0xE12FFF33;
    assert(isTerminatorBranchArm(false, blxLR));
    assert(!isTerminatorBranchArm(true, blxLR));
    assert(isTerminatorBranchArm(false, blxR3));
    assert(isTerminatorBranchArm(true, 0xE12FFF13)); // ARM7 BX
    assert(!isTerminatorBranchArm(false, 0xF12FFF3E)); // reserved condition
    for (uint32_t target : {0x2001u, 0x3000u}) {
        Interpreter cpu(false);
        cpu.registersUsr[15] = 0x1008;
        cpu.registersUsr[14] = target;
        cpu.cpsr = 0x10;
        int cycles = execBranchArm(&cpu, blxLR, 8, 8, 2);
        assert(cycles == 5 && cpu.flushed);
        assert(cpu.registersUsr[14] == 0x100C); // R15 + remDelta - 4
        assert(cpu.registersUsr[15] == target); // old LR, not new LR
        assert((cpu.cpsr & 0x20) == ((target & 1) << 5));
    }
    Interpreter cpu(false);
    cpu.registersUsr[15] = 0x1008;
    cpu.registersUsr[3] = 0x3001;
    assert(execBranchArm(&cpu, blxR3, 0, 0, 0) == 3);
    assert(cpu.registersUsr[14] == 0x1004 && cpu.registersUsr[15] == 0x3001);
    assert(cpu.cpsr & 0x20);
    // A failed condition must neither link nor switch state; it consumes one cycle.
    uint8_t pipelineBytes[24] = {};
    Interpreter predicated(false);
    predicated.registersUsr[15] = 0x1008;
    predicated.registersUsr[14] = 0x2101;
    predicated.pcData = pipelineBytes + 8;
    assert(execBranchArm(&predicated, 0x112FFF3Eu, 4, 4, 2) == 3);
    assert(predicated.registersUsr[15] == 0x100C);
    assert(predicated.registersUsr[14] == 0x2101);
    assert(!predicated.flushed && (predicated.cpsr & 0x20) == 0);
    std::puts("ARM9 BLX Rm branch helper: PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="noods_blx_") as directory:
    path = pathlib.Path(directory)
    (path / "test.cpp").write_text(cpp)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
