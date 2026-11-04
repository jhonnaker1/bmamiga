#!/usr/bin/env python3
"""
make_bench_adf.py - makes the benchmark floppies, which measure how fast
the Amiga's CPU runs: with and without the JIT, in its indirect and direct
modes, on a Pi 4 or 5, or in the PC simulator.

    python3 tools/bench/make_bench_adf.py [folder]

makes bench-cpu.adf and bench-mem.adf there. Each boot block first copies
its loop into fast RAM, as a program would be loaded (the JIT doesn't
compile code in chip RAM; with no fast RAM it stays where it is), and loops
there for ever, adding 1 to a counter at $F0 (an unused exception vector)
after every 1000 times round an inner loop of:

  bench-cpu  register arithmetic (add, eor, ror, addq): the JIT's own gain
  bench-mem  a longword pushed onto the stack and popped back (memory, fast
             RAM if the Amiga has it): what the JIT's indirect mode costs

It needs Kickstart 2.0 or later, or AROS (for exec's CacheClearU()).

With bench=1 (cmdline.txt or bmamiga.cfg), BMAmiga's log says every 10 s
how many 1000s a second ("bench: N loops a second"); in the PC simulator,
the script command "peek 0xf0" prints the counter.

        moveq   #64,d0          ; 7040       the loop's size, at most
        moveq   #4,d1           ; 7204       MEMF_FAST
        jsr     -198(a6)        ; 4EAE FF3A  exec's AllocMem()
        tst.l   d0              ; 4A80
        beq.s   bench           ; 6718       no fast RAM: here
        movea.l d0,a1           ; 2240
        lea     bench(pc),a0    ; 41FA 0014
        moveq   #15,d2          ; 740F       16 longwords
copy:   move.l  (a0)+,(a1)+     ; 22D8
        dbra    d2,copy         ; 51CA FFFC
        move.l  d0,-(sp)        ; 2F00
        jsr     -636(a6)        ; 4EAE FD84  exec's CacheClearU()
        movea.l (sp)+,a0        ; 205F
        jmp     (a0)            ; 4ED0
bench:  lea     $F0.w,a0        ; 41F8 00F0
        clr.l   (a0)            ; 4290
outer:  move.w  #999,d3         ; 363C 03E7
inner:  (the loop's own)
        dbra    d3,inner        ; 51CB disp
        addq.l  #1,(a0)         ; 5290
        bra.s   outer           ; 60 disp

This file is part of BMAmiga, a bare metal Amiga emulator for the
Raspberry Pi. See LICENSE for licensing details.
"""

import os
import struct
import sys

# The inner loops (hand-assembled 68000).
INNER = {
    # add.l d0,d1; eor.l d1,d2; ror.l #3,d2; addq.l #1,d0
    "cpu": bytes.fromhex("D280" "B382" "E69A" "5280"),
    # move.l d0,-(sp); add.l (sp)+,d1; addq.l #1,d0
    "mem": bytes.fromhex("2F00" "D29F" "5280"),
}


# Copies the loop (at 36) into fast RAM and jumps there.
TO_FAST = bytes.fromhex("7040" "7204" "4EAEFF3A" "4A80" "6718" "2240" "41FA0014"
                        "740F" "22D8" "51CAFFFC" "2F00" "4EAEFD84" "205F" "4ED0")


def code(inner):
    head = bytes.fromhex("41F800F0" "4290" "363C03E7")    # outer at 6, inner at 10
    dbra_at = 10 + len(inner)                            # its displacement at + 2
    dbra = bytes.fromhex("51CB") + struct.pack(">h", 10 - (dbra_at + 2))
    bra_at = dbra_at + 4 + 2                             # after addq.l #1,(a0)
    tail = bytes.fromhex("5290") + bytes([0x60, (6 - (bra_at + 2)) & 0xFF])
    return TO_FAST + head + inner + dbra + tail


ADF_SIZE = 80 * 2 * 11 * 512


def boot_block(code_bytes):
    block = bytearray(1024)
    block[0:4] = b"DOS\0"
    struct.pack_into(">I", block, 8, 880)       # the root block, as a disk has it
    block[12:12 + len(code_bytes)] = code_bytes
    # The boot block's checksum: the longwords' sum, with each carry added
    # back in, is 0xFFFFFFFF.
    total = 0
    for (word,) in struct.iter_unpack(">I", bytes(block)):
        total += word
        if total > 0xFFFFFFFF:
            total = (total & 0xFFFFFFFF) + 1
    struct.pack_into(">I", block, 4, ~total & 0xFFFFFFFF)
    return bytes(block)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, inner in INNER.items():
        out = os.path.join(folder, "bench-%s.adf" % name)
        with open(out, "wb") as f:
            f.write(boot_block(code(inner)))
            f.write(bytes(ADF_SIZE - 1024))
        print("%s: %s" % (out, code(inner).hex()))


if __name__ == "__main__":
    main()
