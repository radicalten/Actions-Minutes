Racer 3DS Browser Builder — download-on-build version
=====================================================

1. Extract the entire ZIP. Keep index.html and the assets folder together.
   Do not extract the individual .gz files inside assets.
2. Connect to the internet and open index.html in a recent desktop browser
   (Chrome, Edge, or Firefox).
3. Select the extracted assets folder. Your browser may call its confirmation
   "Upload"; the files are read locally and are not sent to a website.
4. Select your supported US NEPE .z64 ROM, or a ZIP containing one .z64 ROM.
5. Click Build 3DS files. Keep the page open, then download both outputs.

To reduce this ZIP's size, the builder downloads fixed versions of Pyodide and
an LLVM WebAssembly toolchain from jsDelivr and UNPKG while building. The first
build may download tens of megabytes. Your browser may cache these tools, but
internet access can be required again on later builds. Each downloaded tool is
checked against a SHA-256 hash before it runs. The remaining custom tools and
libraries are in the included assets folder and are also verified.

The downloads are GET requests for public tool files only; your ROM and the
resulting game files are processed locally and are not included in requests.
The content-delivery services can see your IP address and the requested tool
URLs. No ROM or ROM-derived game binary is included in this ZIP. Your generated
CIA and 3DSX contain ROM-derived code and should be kept private.

To run the port, put your original uncompressed ROM at
SD:/3ds/racer/baserom.z64. Put Racer3DS.3dsx at SD:/3ds/Racer3DS.3dsx for the
Homebrew Launcher, or install the CIA with a compatible installer.

What this build changes
=======================

Builds made with this ZIP include three fixes. Everything else about the
builder — the ROM check, the in-browser recompilation, the pinned downloadable
toolchain and their hashes — is unchanged.

1. The settings menu's 3D page now works.

   Cause: Racer's display lists carry no transform tagging (no
   gEXMatrixGroup), and the port left the renderer's "unclassified
   perspective" behaviour at its default of no stereo. Every world triangle
   was therefore classified as "no stereo" and received a shift of exactly
   zero pixels, whatever the menu or the 3D slider said.

   Fix: perspective draws are now classified as world geometry, with
   orthographic draws and texture rectangles left as HUD. The page also gained
   a 3D on/off row (off / slider / on) and a 3D strength row (0-100 %), so 3D
   can be turned on and sized with the console's 3D slider at the bottom (as
   it is in an emulator, or on a console whose 3D the player keeps off).
   Both rows are saved in settings.ini.

2. The game can be left through the HOME Menu, and Rosalina's menu no longer
   crashes it.

   Cause: the port took HOME for itself (aptSetHomeAllowed(false) plus its own
   quit prompt), so the HOME Menu was never reachable, and it registered no
   APT hook, so a suspended application kept running its own threads and
   queueing GPU work.

   Fix: HOME belongs to the system again and the custom prompt is gone. A new
   APT hook pauses the renderer, audio, input and the stall watchdog while the
   system has the application suspended (HOME Menu, lid, Rosalina) and resumes
   them afterwards, and asks for the normal shutdown when the title is closed
   from the HOME Menu. The in-game exit is now
   SELECT -> Game -> Quit to HOME Menu.

3. High memory is no longer requested.

   The CIA's exheader now asks for the standard 64 MB application memory mode
   on both console generations (SystemMode 64MB, SystemModeExt Legacy) instead
   of the New 3DS 124 MB extended mode. The port does not need it: the 32 MB
   ROM is read from the SD card as the game asks for it, exactly as it already
   was on an Old 3DS.

Notices, source availability and provenance, including the audio interpreters,
remain in assets/source-bundle.gz (a gzipped ZIP archive), together with the
fetch-enabled builder source and its assembly script. The project source is the
game-source asset (assets/game-source.gz) — the same archive the builder
compiles, including the host-side 3D test under tools/host-tests/ — so there is
no second copy to fall out of step with it.

The builder and port are experimental. Builds can take several minutes and use
significant memory. Physical-hardware and audio behavior remain unverified.
