// Stubs for the GW ATEX pre-DXT unpack stages that upstream are 32-bit x86 asm
// (AtexAsm.cpp SubCode3/4/5/7). datcore does not run the asm; instead a texture
// whose CompressionCode needs one of these stages is flagged as "needed asm" so
// callers can skip it (or route it to the Windows build). SubCode1/2, which
// cover a real subset of textures, are already pure C in atex_decompress.cpp.
#include "datcore/atex_asm.h"

namespace {
bool g_needed_asm = false;
}

void atex_reset_asm_flag() { g_needed_asm = false; }
bool atex_needed_asm() { return g_needed_asm; }

void AtexSubCode3_Asm(uint32_t*, uint32_t*, uint32_t*, SImageData*, unsigned int, unsigned int) {
    g_needed_asm = true;
}
void AtexSubCode4_Asm(uint32_t*, uint32_t*, uint32_t*, SImageData*, unsigned int, unsigned int) {
    g_needed_asm = true;
}
void AtexSubCode5_Asm(uintptr_t, uintptr_t, uintptr_t, uintptr_t, unsigned int, unsigned int, unsigned int) {
    g_needed_asm = true;
}
void AtexSubCode7_Asm(uintptr_t, unsigned int) {
    g_needed_asm = true;
}
