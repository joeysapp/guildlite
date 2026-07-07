#pragma once
#include <cstdint>
// The GW ATEX pre-DXT unpack stages. In datcore, SubCode3/4/5/7 (x86 asm
// upstream in AtexAsm.cpp) are STUBBED in atex_stub.cpp — only SubCode1/2
// (already pure C in AtexDecompress.cpp) are implemented. A texture whose
// CompressionCode requires a stubbed stage sets the "needed asm" flag and
// decodes only partially. SubCode1/2 _Asm are declared but never called.
struct SImageData;

void AtexSubCode3_Asm(uint32_t* outBuffer, uint32_t* dcmpBuffer1, uint32_t* dcmpBuffer2,
                      SImageData* imageData, unsigned int blockCount, unsigned int blockSize);
void AtexSubCode4_Asm(uint32_t* outBuffer, uint32_t* dcmpBuffer1, uint32_t* dcmpBuffer2,
                      SImageData* imageData, unsigned int blockCount, unsigned int blockSize);
void AtexSubCode5_Asm(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d,
                      unsigned int e, unsigned int f, unsigned int g);
void AtexSubCode7_Asm(uintptr_t a, unsigned int b);

// datcore stub bookkeeping: reset before a decode, query after.
void atex_reset_asm_flag();
bool atex_needed_asm();
