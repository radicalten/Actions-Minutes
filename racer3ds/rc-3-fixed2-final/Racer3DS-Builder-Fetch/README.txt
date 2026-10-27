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

After choosing the assets folder, click "Download source and licenses" for
project source, notices, source availability and provenance, including the audio
interpreters. The fetch-enabled builder source and assembly script are included.

The builder and port are experimental. Builds can take several minutes and use
significant memory. Physical-hardware and audio behavior remain unverified.
