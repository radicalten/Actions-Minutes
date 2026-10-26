# The game configuration the recompiler works from

Copied from [sp00nznet/racer](https://github.com/sp00nznet/racer) (MIT) so the 3DS tree can
validate a ROM and recompile it without asking you to dig through a PC checkout:

| File | What it is |
|---|---|
| `symbols.toml` | 906 function entries: VRAM, size, name. Everything the recompiler needs to know about layout. |
| `recomp.toml` | the recompiler project file: input ROM, entrypoint `0x80000400`, stubs/patches for libultra, output settings. |
| `rsp/aspMain.toml` | the audio microcode task file (text at ROM `0x9A050`, 16 command handler addresses). |

`platform/3ds/tools/gen_recomp_plan.py` turns `symbols.toml` into
`platform/3ds/include/recomp_plan.h`, which is what the console compiles in to check your ROM
at boot. Regenerate it if you edit the symbol map:

```sh
python3 platform/3ds/tools/gen_recomp_plan.py \
    --symbols game-config/symbols.toml --out platform/3ds/include/recomp_plan.h
```

None of this is game data: it is a list of addresses, derived from analysis of the ROM. No
ROM content is stored in this tree, and the recompiled output (`game/`) is ROM-derived — it
is gitignored and must not be redistributed.
