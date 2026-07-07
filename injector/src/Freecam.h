#pragma once

struct IDirect3DDevice9;

// Client-side free camera. Detaches GW's camera from the character (GWCA UnlockCam) and flies
// it with WASD/QE/ZX, mirroring the proven GWToolbox CameraUnlockModule fly model -- translate
// look_at_target + rotate yaw, then UpdateCameraPos() -- but lifted off ToolboxModule/Chat onto
// our own EndScene loop and overlay, with input read via GetAsyncKeyState. Draw() runs the fly
// step (whenever freecam is engaged) plus the control window; Shutdown() re-locks the camera so
// an unload/reload never strands the player detached. Same shape as Exporter so the overlay
// hosts it identically (panel-bar toggle + per-frame Draw + control verbs).
namespace Freecam {
    void Init();
    void Draw(IDirect3DDevice9* device);  // per-frame: fly step (if engaged) + control window
    void Shutdown();                      // re-lock the camera (UnlockCam(false))
    void Command(const char* verb);       // "freecam on|off|toggle", "cam speed <n>", "fov <n>", "fog on|off"
    bool& WindowVisible();                // control-window open flag -- for the overlay's panel bar
}
