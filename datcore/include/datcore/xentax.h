#pragma once
// Guild Wars .dat block decompressor (Huffman + LZ), ported from GWMB / GWDatBrowser.
// `output` is allocated with new[] by the decoder and must be delete[]'d by the caller.
// On failure `output` is set to nullptr.
void UnpackGWDat(unsigned char* input, int insize, unsigned char*& output, int& outsize);
