#pragma once
#include <string>
#include <vector>

namespace gl {

// Result of running a child process.
struct ProcResult {
    int  exitCode = -1;      // process exit status, or -1 if it never started
    bool spawned  = false;   // false => couldn't even spawn (e.g. scp not on PATH)
    std::string output;      // combined stdout + stderr
};

// Run argv[0] with the given arguments via posix_spawnp -- NO shell, so there is no
// quoting/injection surface even though host/path come from user-editable fields.
// Blocks until the child exits; call it OFF the render thread. This is the one file an
// iOS build replaces: iOS forbids spawning /usr/bin/ssh, so its transport would use an
// in-process SSH lib (libssh2) or a relay instead of this.
ProcResult Run(const std::vector<std::string>& argv);

} // namespace gl
