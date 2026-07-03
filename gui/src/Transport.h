#pragma once
#include <memory>
#include <string>

namespace gl {

// Where an injected client's control file lives, and how to peek its log. Defaults match
// the standalone injector: the stub watches Documents\guildlite\control at ~2 Hz (INJECTOR.md).
struct Target {
    std::string host        = "guildlite-win";                 // ssh Host alias (~/.ssh/config)
    std::string controlPath = "Documents/guildlite/control";   // the stub polls this
    std::string logCommand  =                                  // best-effort remote log peek
        "powershell -NoProfile -Command \"Get-Content -Tail 60 "
        "$env:USERPROFILE\\Documents\\guildlite\\*.log\"";
    int connectTimeout = 6;   // seconds; a dead host fails fast instead of hanging the send
};

struct SendResult {
    bool ok = false;
    std::string detail;   // one human-readable line for the log pane
};

// Sink for control verbs. The macOS impls shell out to scp/ssh; an iOS build swaps in an
// in-process SSH (libssh2) or relay impl behind this same interface -- the console never
// knows the difference.
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual const char* name() const = 0;
    virtual SendResult  send(const std::string& payload) = 0;   // write verbs to the control file
    virtual SendResult  fetchLog() { return {false, "log fetch not supported"}; }
};

// Logs what it WOULD send; no network. Pure ImGui/UI development, works fully offline.
std::unique_ptr<ITransport> MakeMockTransport();

// scp the payload to <host>:<controlPath>; ssh <host> <logCommand> for the log peek.
std::unique_ptr<ITransport> MakeSshTransport(Target t);

} // namespace gl
