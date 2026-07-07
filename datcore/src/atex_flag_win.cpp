// Windows build: the real x86 asm (atex_asm_win.cpp) decodes every ATEX stage,
// so a decode is never "missing" — the needed-asm flag is always false.
// (On macOS/clang this is replaced by atex_stub.cpp, which sets the flag when a
// stubbed stage is hit.)
#include "datcore/atex_asm.h"

void atex_reset_asm_flag() {}
bool atex_needed_asm() { return false; }
