#!/usr/bin/env python3
# Turn the mGBA test suite's CPU-core tables into vectors for jit3.
#
# The suite (mgba-emu/suite, the ROM the user pointed at: mattrbeck/mgba-suite-auto
# builds it) is endrift's hardware-accuracy suite.  Its CPU-core sections carry
# explicit expected results *and* expected CPSRs for every case:
#
#   src/shifter.c        "movs r0, r1, <shift>" over every shift form and edge
#                        amount, with { rd, cpsr } per case, compared as whole
#                        32-bit words.
#   src/carry.c          "adcs/sbcs/rscs out, in0, in1" over 8 operand pairs x
#                        carry-in 0/1, with result+CPSR per operation.
#   src/multiply-long.c  "smulls/umulls/smlals/umlals r9, r8, r0, r1", 32 cases,
#                        with both result halves and a CPSR each - this is where
#                        the suite disagrees with NooDS (which writes no flags).
#
# Two output files:
#   suite_vectors.txt      Thumb vectors: the same operations, mapped onto the Thumb
#                          encoding that performs them (thumb_probe.cpp -suite).
#   suite_vectors_arm.txt  ARM vectors: the suite's own instructions, encoded
#                          (jit3_test.cpp -suite).  multiply-long exists only here.
#
# Cases with no Thumb form or no encodable form are listed as skipped with the
# reason, never silently dropped.
#
# Usage: gen_suite_vectors.py [suite-checkout] [out-file]
#        (default checkout /home/user/gba-suite, default out ./suite_vectors.txt)
import re
import sys
import os

SRC = sys.argv[1] if len(sys.argv) > 1 else '/home/user/gba-suite'
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), 'suite_vectors.txt')

T_BIT = 0x20
MODE_SYS = 0x1F

# Thumb encodings used below:
#   format 1  LSL/LSR/ASR Rd, Rm, #imm5   = (kind << 11) | (imm5 << 6) | (rm << 3) | rd
#   format 3  MOV/CMP/ADD/SUB Rd, #imm8   = 0x2000 | (kind << 11) | (rd << 8) | imm8
#   format 4  ALU row   Rd, Rs            = 0x4000 | (aluop << 6) | (rs << 3) | rd
ALU = {'lsl': 2, 'lsr': 3, 'asr': 4, 'ror': 7, 'adc': 5, 'sbc': 6}


def parse_num(t):
    t = t.strip()
    return int(t, 16) if t.lower().startswith('0x') else int(t)


def src_path(name):
    # a checkout has the C sources under src/, a bare download has them at the top
    for cand in (os.path.join(SRC, 'src', name), os.path.join(SRC, name)):
        if os.path.exists(cand):
            return cand
    raise SystemExit('cannot find %s under %s' % (name, SRC))


def rows(path, pat):
    text = open(path, encoding='utf-8', errors='replace').read()
    return [m.groups() for m in re.finditer(pat, text)]


# ---------------------------------------------------------------------------
# Thumb
# ---------------------------------------------------------------------------
def shifter_vectors():
    path = src_path('shifter.c')
    # { "name", fn, inCpsr, in0, in1, { rd, cpsr } },
    pat = (r'\{\s*"([^"]*)",\s*(\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),'
           r'\s*\{\s*([^,]+),\s*([^}]+)\}\s*\}')
    out, skipped = [], []
    for name, fn, csr, in0, in1, rd, ecsr in rows(path, pat):
        try:
            csr, in0, in1 = parse_num(csr), parse_num(in0), parse_num(in1)
            rd, ecsr = parse_num(rd), parse_num(ecsr)
        except ValueError:
            # PC-operand rows carry expressions like `ADDR(movPC) + 0xFFF`: the
            # expectation is a link address, not a value.
            skipped.append((name, 'expectation is a link-address expression'))
            continue
        cin = csr | MODE_SYS | T_BIT
        eout = ecsr | T_BIT
        note = 'suite shifter: movs r0, r1, %s' % name

        def emit(op, dst, note):
            out.append((name, op, in0, in1, cin, dst, rd, eout, note))

        m = re.fullmatch(r'movImm([0-9A-F]+)', fn)
        if m:
            val = int(m.group(1), 16)
            if val > 0xFF:
                skipped.append((name, 'no Thumb imm8 MOV form (ARM rotated immediate)'))
            else:
                emit(0x2000 | (0 << 11) | (0 << 8) | val, 0, note + ' -> movs r0, #%X' % val)
            continue
        m = re.fullmatch(r'movR1(Lsl|Lsr|Asr)(\d+)', fn)
        if m:
            kind = {'Lsl': 0, 'Lsr': 1, 'Asr': 2}[m.group(1)]
            amt = int(m.group(2))
            imm5 = amt if amt < 32 else (0 if amt == 32 else None)
            if imm5 is None:
                skipped.append((name, 'shift amount %d is not encodable' % amt))
            else:
                op = (kind << 11) | (imm5 << 6) | (1 << 3) | 0
                emit(op, 0, note + ' -> %ss r0, r1, #%d' % (m.group(1).lower(), amt))
            continue
        m = re.fullmatch(r'movR1(Lsl|Lsr|Asr|Ror)R0', fn)
        if m:
            kind = m.group(1).lower()
            op = 0x4000 | (ALU[kind] << 6) | (0 << 3) | 1     # rd = r1 (the value), rs = r0
            emit(op, 1, note + ' -> %ss r1, r0  (dest is r1: Thumb takes rd as an operand)'
                 % kind)
            continue
        if re.fullmatch(r'movR1Ror\d+', fn) or fn == 'movR1Rrx':
            skipped.append((name, 'no Thumb ROR-immediate / RRX form'))
            continue
        if fn.startswith('movPC') or fn.startswith('addR0PCR1'):
            skipped.append((name, 'r15 operand: jit3 refuses PC operands by design '
                                  '(and the expectation embeds the test link address)'))
            continue
        if fn == 'movR1':
            skipped.append((name, 'Thumb has no flag-setting register MOV'))
            continue
        skipped.append((name, 'unmapped function %s' % fn))
    return out, skipped


def carry_vectors():
    path = src_path('carry.c')
    pat = r'\{\s*"([^"]*)",\s*\{([^}]*)\}\s*,?\s*\}'
    out, skipped = [], []
    for name, inner in rows(path, pat):
        v = [parse_num(x) for x in inner.split(',') if x.strip()]
        if len(v) != 9:
            skipped.append((name, 'row has %d fields' % len(v)))
            continue
        in0, in1, csr, adc, adc_c, sbc, sbc_c, rsc, rsc_c = v
        cin = csr | MODE_SYS | T_BIT
        out.append((name + ' / ADC', 0x4000 | (ALU['adc'] << 6) | (1 << 3) | 0,
                    in0, in1, cin, 0, adc, adc_c | T_BIT, 'suite carry: adcs out, in0, in1'))
        out.append((name + ' / SBC', 0x4000 | (ALU['sbc'] << 6) | (1 << 3) | 0,
                    in0, in1, cin, 0, sbc, sbc_c | T_BIT, 'suite carry: sbcs out, in0, in1'))
        skipped.append((name + ' / RSC', 'RSC has no Thumb form (expected %08X)' % rsc))
    return out, skipped


# ---------------------------------------------------------------------------
# ARM
#
# The suite's shifter/carry tables are *already* ARM instructions (the GBA is an
# ARM7TDMI), so unlike the Thumb mapping these need only the encoding, not a
# translation.  multiply-long is ARM-only and is where the suite disagrees with
# NooDS (see the note in NOTES.md).
#
# Output line: name|op|r0|r1|r2|r3|r8|r9|cpsr_in|checks|note
#   checks = comma-separated regN=XXXXXXXX / cpsr=XXXXXXXX
# ---------------------------------------------------------------------------
def arm_imm(value, opcode=13, s=1, rd=0):
    """`opcode{S} rd, #imm` in ARM immediate encoding (None when not encodable).

    The opcode used to be left at 0, which encoded every "movs r0, #imm" row as
    "and r0, r0, #imm" and quietly turned four of the suite's shifter rows into
    pass-by-accident cases (they also had a 9-field line shape, which the old
    runner skipped - two bugs hiding each other).
    """
    for rot in range(16):
        if rot == 0:
            imm8 = value
        else:
            imm8 = ((value << (2 * rot)) | (value >> (32 - 2 * rot))) & 0xFFFFFFFF
        if imm8 <= 0xFF:
            return ((14 << 28) | (opcode << 21) | (s << 20) | (rd << 12) |
                    (1 << 25) | (rot << 8) | imm8)
    return None


def arm_dp(opcode, s, rn, rd, op2, cond=14):
    return (cond << 28) | (opcode << 21) | (s << 20) | (rn << 16) | (rd << 12) | op2


def arm_movs_r0_r1_shift(shift_type, amount):
    """movs r0, r1, <type> #amount  (amount 0 is the encodable special case)."""
    return arm_dp(13, True, 0, 0, (amount << 7) | (shift_type << 5) | 1)


def arm_movs_r0_r1_reg(shift_type):
    """movs r0, r1, <type> r0 - the suite puts the amount in r0 and the value in r1."""
    return arm_dp(13, True, 0, 0, (0 << 8) | (shift_type << 5) | (1 << 4) | 1)


ARM7_ROWS = set()   # rows whose expectation is ARM7TDMI-specific, not architectural


def arm_vectors():
    out, skipped = [], []
    # Rows whose expectation is ARM7TDMI-specific rather than architectural.  The
    # multiply-long table's C flag is the ARM7TDMI multiplier's carry-out; nothing in
    # a GBA suite can speak for the DS's ARM946E-S, so those rows are checked on the
    # ARM7 path and, on the ARM9 path, only for the result registers and N/Z.
    global ARM7_ROWS
    arm7_rows = ARM7_ROWS

    # ---- shifter: the same rows as the Thumb mapping, in their native encoding
    path = src_path('shifter.c')
    pat = (r'\{\s*"([^"]*)",\s*(\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),'
           r'\s*\{\s*([^,]+),\s*([^}]+)\}\s*\}')
    shift_map = {'Lsl': 0, 'Lsr': 1, 'Asr': 2}
    for name, fn, csr, in0, in1, rd, ecsr in rows(path, pat):
        try:
            csr, in0, in1 = parse_num(csr), parse_num(in0), parse_num(in1)
            rd, ecsr = parse_num(rd), parse_num(ecsr)
        except ValueError:
            skipped.append((name, 'link-address expectation'))
            continue
        cin = csr | MODE_SYS
        checks = 'r0=%08X,cpsr=%08X' % (rd, ecsr)

        def emit(op, note):
            out.append((name, op, in0, in1, 0, 0, 0, 0, cin, checks, note))

        m = re.fullmatch(r'movImm([0-9A-F]+)', fn)
        if m:
            op = arm_imm(int(m.group(1), 16))
            if op is None:
                skipped.append((name, 'not encodable as an ARM immediate'))
            else:
                emit(op, 'suite shifter: movs r0, #%s' % m.group(1))
            continue
        m = re.fullmatch(r'movR1(Lsl|Lsr|Asr)(\d+)', fn)
        if m:
            amt = int(m.group(2))
            if amt > 31 and not (amt == 32 and m.group(1) in ('Lsr', 'Asr')):
                skipped.append((name, 'shift amount %d is not encodable' % amt))
            else:
                emit(arm_movs_r0_r1_shift(shift_map[m.group(1)], amt % 32),
                     'suite shifter: movs r0, r1, %s #%d' % (m.group(1).lower(), amt))
            continue
        m = re.fullmatch(r'movR1Ror(\d+)', fn)
        if m:
            emit(arm_movs_r0_r1_shift(3, int(m.group(1)) % 32),
                 'suite shifter: movs r0, r1, ror #%s' % m.group(1))
            continue
        if fn == 'movR1Rrx':
            emit(arm_movs_r0_r1_shift(3, 0), 'suite shifter: movs r0, r1, rrx')
            continue
        m = re.fullmatch(r'movR1(Lsl|Lsr|Asr|Ror)R0', fn)
        if m:
            emit(arm_movs_r0_r1_reg(shift_map.get(m.group(1), 3)),
                 'suite shifter: movs r0, r1, %s r0' % m.group(1).lower())
            continue
        if fn == 'movR1':
            emit(arm_dp(13, True, 0, 0, 1), 'suite shifter: movs r0, r1')
            continue
        if fn.startswith('movPC') or fn.startswith('addR0PCR1'):
            skipped.append((name, 'r15 operand: refused by design'))
            continue
        skipped.append((name, 'unmapped function %s' % fn))

    # ---- carry: adcs / sbcs / rscs out, in0, in1
    path = src_path('carry.c')
    pat2 = r'\{\s*"([^"]*)",\s*\{([^}]*)\}\s*,?\s*\}'
    for name, inner in rows(path, pat2):
        v = [parse_num(x) for x in inner.split(',') if x.strip()]
        if len(v) != 9:
            skipped.append((name, 'row has %d fields' % len(v)))
            continue
        in0, in1, csr, adc, adc_c, sbc, sbc_c, rsc, rsc_c = v
        cin = csr | MODE_SYS
        for opc, res, ecsr, what in ((5, adc, adc_c, 'adcs'), (6, sbc, sbc_c, 'sbcs'),
                                     (7, rsc, rsc_c, 'rscs')):
            out.append((name + ' / ' + what.upper(), arm_dp(opc, True, 0, 2, 1),
                        in0, in1, 0, 0, 0, 0, cin,
                        'r2=%08X,cpsr=%08X' % (res, ecsr),
                        'suite carry: %s r2, r0, r1' % what))

    # ---- multiply long (ARM only; RdLo = r9, RdHi = r8 as the suite's asm uses)
    path = src_path('multiply-long.c')
    pat3 = r'\{\s*"([^"]*)",\s*\{([^}]*)\}\s*,?\s*\}'
    for name, inner in rows(path, pat3):
        v = [parse_num(x) for x in inner.split(',') if x.strip()]
        # A row without an accumulate addend lists only in0/in1 and the two plain
        # multiply expectations (8 values); the C initializer leaves the rest zero.
        if len(v) == 8:
            in0, in1, sm_hi, sm_lo, sm_c, um_hi, um_lo, um_c = v
            in2 = in3 = sa_hi = sa_lo = sa_c = ua_hi = ua_lo = ua_c = 0
        elif len(v) == 16:
            (in0, in1, sm_hi, sm_lo, sm_c, um_hi, um_lo, um_c,
             in2, in3, sa_hi, sa_lo, sa_c, ua_hi, ua_lo, ua_c) = v
        else:
            skipped.append((name, 'row has %d fields' % len(v)))
            continue
        cin = MODE_SYS
        for u, a, hi, lo, ec, what in ((0, 0, sm_hi, sm_lo, sm_c, 'smulls'),
                                       (1, 0, um_hi, um_lo, um_c, 'umulls'),
                                       (0, 1, sa_hi, sa_lo, sa_c, 'smlals'),
                                       (1, 1, ua_hi, ua_lo, ua_c, 'umlals')):
            if a and not (in2 or in3):
                # the suite itself skips the accumulate subtests without an addend,
                # so its expectation for them was never checked on hardware
                skipped.append((name + ' / ' + what, 'no addend: the suite skips it too'))
                continue
            op = ((14 << 28) | (1 << 23) | (u << 22) | (a << 21) | (1 << 20) |
                  (8 << 16) | (9 << 12) | (1 << 8) | 0x90 | 0)
            # the suite's asm does `ldr r8, in3` / `ldr r9, in2` before the op: the
            # addend's high word belongs in r8 (RdHi) and the low word in r9 (RdLo)
            label = name + ' / ' + what.upper()
            arm7_rows.add(label)
            out.append((label, op, in0, in1, 0, 0,
                        in3 if a else 0, in2 if a else 0, cin,
                        'r8=%08X,r9=%08X,cpsr=%08X' % (hi, lo, ec),
                        'suite multiply-long: %s r9, r8, r0, r1' % what))
    return out, skipped


def main():
    vec, skip1 = shifter_vectors()
    cv, skip2 = carry_vectors()
    vec += cv
    skip1 += skip2
    with open(OUT, 'w') as f:
        f.write('# Thumb vectors derived from the mGBA test suite (mgba-emu/suite).\n'
                '# Generated by tools/host/gen_suite_vectors.py - do not edit by hand.\n'
                '# name|op|r0|r1|cpsr_in|dst|exp_reg|exp_cpsr|note\n')
        for (name, op, r0, r1, cin, dst, exp, ecsr, note) in vec:
            f.write('%s|%04X|%08X|%08X|%08X|%d|%08X|%08X|%s\n'
                    % (name, op, r0, r1, cin, dst, exp, ecsr, note))
    print('thumb vectors: %d into %s' % (len(vec), OUT))
    print('thumb skipped: %d' % len(skip1))
    seen = {}
    for name, why in skip1:
        seen.setdefault(why, []).append(name)
    for why, names in sorted(seen.items(), key=lambda kv: -len(kv[1]))[:4]:
        print('  %-66s x%d' % (why, len(names)))

    arm, skiparm = arm_vectors()
    armout = OUT.replace('.txt', '_arm.txt') if OUT.endswith('.txt') else OUT + '_arm'
    with open(armout, 'w') as f:
        f.write('# ARM vectors derived from the mGBA test suite (mgba-emu/suite).\n'
                '# Generated by tools/host/gen_suite_vectors.py - do not edit by hand.\n'
                '# core|name|op|r0|r1|r2|r3|r8|r9|cpsr_in|checks|note\n'
                '# core=arm7 -> the expectation is ARM7TDMI-specific (multiply-long C)\n'
                '# core=any  -> architectural: checked on the ARM9 path as well\n')
        for (name, op, r0, r1, r2, r3, r8, r9, cin, checks, note) in arm:
            f.write('%s|%s|%08X|%08X|%08X|%08X|%08X|%08X|%08X|%08X|%s|%s\n'
                    % ('arm7' if name in ARM7_ROWS else 'any',
                       name, op, r0, r1, r2, r3, r8, r9, cin, checks, note))
    print('arm vectors: %d into %s' % (len(arm), armout))
    print('arm skipped: %d' % len(skiparm))
    seen = {}
    for name, why in skiparm:
        seen.setdefault(why, []).append(name)
    for why, names in sorted(seen.items(), key=lambda kv: -len(kv[1]))[:4]:
        print('  %-66s x%d' % (why, len(names)))


if __name__ == '__main__':
    main()
