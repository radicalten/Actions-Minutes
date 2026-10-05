# GBA Compilation Builder

A simple **web app** for the Python 3 builder scripts from
[patters-match/gba-emu-compilation-builders](https://github.com/patters-match/gba-emu-compilation-builders).

It wraps all 12 build programs in a beginner-friendly three-step interface:

1. **Choose builder** — PocketNES, Goomba/Jagoomba, SNESAdvance, Snezziboy, PCEAdvance,
   SMSAdvance, HVCA, Cologne, MSXAdvance, NGPGBA, Wasabi or ZXAdvance
2. **Add your files** — drag & drop your game ROMs; the app tells you exactly which extra
   files each builder needs (emulator binary, BIOS, …) and can download the recommended
   emulator for you automatically
3. **Build & download** — press one button and get your `.gba` ROM

## Two ways to run it

### 1. Just open the file (no server, no install)

Double-click **`index.html`** — that's it.

The page detects that no server is running and switches to *browser mode*: the real
Python build scripts run **inside the page** (via [Pyodide](https://pyodide.org),
a Python engine for the browser, loaded once from a CDN and then cached).
Nothing is installed and your files never leave your computer.

* First open needs an internet connection (to fetch the ~10 MB Python engine and,
  optionally, the recommended emulator binaries). Everything still works offline
  afterwards except the automatic emulator download — you can always add your own
  emulator file instead.
* Browser mode keeps everything in memory, so stick to regular cartridge ROMs.
  For very large CD-ROM images (`.iso`, hundreds of MB) use the server below.

### 2. With the local server (fastest)

```
python3 server.py
```

…then open **http://localhost:8000**. Or just double-click `start.sh` (macOS/Linux) /
`start.bat` (Windows), which starts the server and opens your browser for you.

Requirements: **Python 3.8+** — nothing else (no pip packages).

The page will automatically use the server when it's running (shown as
“Local build server” at the top) and fall back to browser mode when it isn't.

## Folder layout

```
some-folder/
├── gba-emu-compilation-builders/   <- the scripts + game databases
└── gba-builder/                    <- this app
    ├── index.html                  <- the app (self-contained, works standalone)
    ├── server.py                   <- optional local server
    ├── builder_core.py             <- shared build logic (used by both)
    ├── embed_files.py              <- regenerates the embedded block in index.html
    ├── start.sh / start.bat        <- one-click server start + open browser
    └── jobs/                       <- build sessions (safe to delete)
```

If you change `builder_core.py` or update the scripts repo, run
`python3 embed_files.py` to refresh the copies embedded in `index.html`.

## Notes

- **Everything stays local.** Game ROMs and BIOS files you add are only processed on
  your own machine — nothing is uploaded to the internet. The only network use is the
  optional *“Download emulator for me”* button (and the first-load of the browser
  engine), which fetch freely-distributed files from
  [gba-ezflash-iv-emulators](https://github.com/patters-match/gba-ezflash-iv-emulators)
  and cdn.jsdelivr.net.
- **BIOS files** for Cologne (ColecoVision BIOS) and MSXAdvance (MSX BIOS) cannot be
  distributed with the app — the interface tells you what to look for and where these
  files are commonly found.
- The **exact same builder scripts** run as from the command line, so output is identical;
  the app simply fills in the arguments for you. Anything the scripts print appears in the
  build log.

## Credits

- Builder scripts by [patters](https://github.com/patters-match) —
  [gba-emu-compilation-builders](https://github.com/patters-match/gba-emu-compilation-builders)
- Emulators by Loopy, FluBBa, Dwedit, TheHiVE, bubble2k, Jaga and others (see each
  emulator’s own page)

Only use game ROMs and BIOS images you legally own.
