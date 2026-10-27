Racer 3DS Offline Builder (split-file version)
==============================================

1. Extract the entire ZIP to a folder. Keep index.html and the assets folder
   together. Do not unzip the individual .gz files inside assets.
2. Open index.html in a recent desktop browser (Chrome, Edge, or Firefox).
3. Under "Select the assets folder", choose the extracted assets folder.
   Browsers require this permission before a local HTML file can read the
   toolchain. Some browsers label the confirmation "Upload"; the builder makes
   no network requests and reads the files locally. Select the folder again
   after reloading the page.
4. Select your supported US NEPE .z64 ROM, or a ZIP containing one .z64 ROM.
5. Click Build 3DS files. Keep the page open, then download both outputs.

The builder works offline and does not need a server, installer, or native
helper. No ROM or generated game code is included in this package. Your ROM
and generated code remain local; the resulting CIA and 3DSX contain ROM-derived
code and should be kept private. Tool assets are checked against SHA-256 values
in index.html when they are read.

To run the port, put your original uncompressed ROM at
SD:/3ds/racer/baserom.z64. Put Racer3DS.3dsx at SD:/3ds/Racer3DS.3dsx for the
Homebrew Launcher, or install the CIA with a compatible installer.

On the builder page, choose the assets folder and click "Download source and
licenses" for the project source, third-party notices, source availability,
and provenance, including the audio interpreters. The split UI source and an
HTML assembly script are included in that source archive.

This remains experimental. Builds can take several minutes and use significant
memory. Physical-hardware, audio, and browser/OS behavior may differ from the
tested emulator and Linux browser runs.
