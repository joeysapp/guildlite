#pragma once

struct IDirect3DDevice9;

// GWCA lifecycle glue -- the standalone replacement for the GWCA bring-up that the
// GWToolbox plugin got for free from ToolboxPlugin::Initialize (INJECTOR.md seam 4).
//
// Whoever OWNS the process's single gwca.dll instance calls Initialize() once on a
// worker thread and Terminate() on unload: that's the monolith's dllmain, or the
// Phase-2 stub. A hosted core (Phase 2) does NOT re-init GWCA -- it calls
// MarkReadyHosted() instead, trusting the stub to have done it. Ready() gates every
// GWCA read in the exporter so we never touch game memory before GWCA has scanned it.
namespace Game {
    bool Initialize();       // GW::Initialize() + EnableHooks(), with retry; flips Ready() on success
    void Terminate();        // GW::DisableHooks() + Terminate(), but only if THIS module Initialized
    void MarkReadyHosted();  // core path: the host already brought GWCA up; just flip Ready()
    bool Ready();            // true once GWCA is safe to read

    // The game's LIVE D3D9 device (GW::Render::GetDevice), retried until non-null. We hook
    // its vtable directly -- far more robust than the spike's throwaway-device trick, which
    // failed with E_INVALIDARG at runtime. Requires GWCA up (call after Initialize/hosted).
    IDirect3DDevice9* GetDevice();
}
