#include "Screenshot.h"

#include <windows.h>
#include <d3d9.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <atomic>
#include <vector>

namespace {
    std::atomic<bool> g_request{false};
}

void Screenshot::Request() { g_request.store(true); }

bool Screenshot::Consume()
{
    bool expected = true;
    return g_request.compare_exchange_strong(expected, false);
}

bool Screenshot::CaptureBackbuffer(IDirect3DDevice9* dev, const std::filesystem::path& out)
{
    IDirect3DSurface9* back = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return false;

    D3DSURFACE_DESC desc{};
    back->GetDesc(&desc);

    // The backbuffer lives in VRAM and usually isn't lockable; copy it into a SYSTEMMEM
    // surface we can read on the CPU.
    IDirect3DSurface9* sys = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr))
        && SUCCEEDED(dev->GetRenderTargetData(back, sys))) {
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            const int w = static_cast<int>(desc.Width);
            const int h = static_cast<int>(desc.Height);
            std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
            const auto* base = static_cast<const unsigned char*>(lr.pBits);
            // GW's backbuffer is X8R8G8B8/A8R8G8B8 -> bytes are B,G,R,(A) little-endian.
            // Swizzle to RGBA and force opaque alpha.
            for (int y = 0; y < h; ++y) {
                const unsigned char* row = base + static_cast<size_t>(y) * lr.Pitch;
                unsigned char* dst = rgba.data() + static_cast<size_t>(y) * w * 4;
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = row[x * 4 + 2];
                    dst[x * 4 + 1] = row[x * 4 + 1];
                    dst[x * 4 + 2] = row[x * 4 + 0];
                    dst[x * 4 + 3] = 255;
                }
            }
            sys->UnlockRect();

            // Encode to memory, then write via the wide path (unicode-safe) to a .tmp and
            // atomically rename -- an SSH fetch never sees a half-written PNG.
            struct Sink { std::vector<unsigned char> bytes; } sink;
            const auto cb = [](void* ctx, void* data, int size) {
                auto* s = static_cast<Sink*>(ctx);
                const auto* p = static_cast<unsigned char*>(data);
                s->bytes.insert(s->bytes.end(), p, p + size);
            };
            if (stbi_write_png_to_func(cb, &sink, w, h, 4, rgba.data(), w * 4) && !sink.bytes.empty()) {
                std::filesystem::path tmp = out;
                tmp += L".tmp";
                const HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD wrote = 0;
                    const BOOL wok = WriteFile(f, sink.bytes.data(), static_cast<DWORD>(sink.bytes.size()), &wrote, nullptr);
                    CloseHandle(f);
                    if (wok && wrote == sink.bytes.size())
                        ok = MoveFileExW(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
                    if (!ok) DeleteFileW(tmp.c_str());
                }
            }
        }
        sys->Release();
    }
    back->Release();
    return ok;
}
