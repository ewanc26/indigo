# C++ source boundary

Indigo's native 3DS application remains C-first. Keep C translation units in the existing `src/` module tree; put new C++ translation units in this top-level `cpp/` directory.

The root Makefile deliberately discovers `.c` files only under `SOURCES` and `.cpp` files only under `CPPSOURCES`. This keeps language ownership visible in the build and avoids quietly mixing C++ into the C modules. C++ uses the existing `CXXFLAGS` policy in the Makefile (GNU C++17, with exceptions and RTTI disabled for binary size). Do not move existing C files merely to make the split look symmetrical.

Keep the 3DS platform lifecycle, rendering, and input code in the established C modules unless C++ provides a concrete benefit. Any C++ code must remain compatible with devkitARM and the static-linking constraints of the 3DS homebrew target.