#include "Proc.h"

#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstring>

extern char** environ;

namespace gl {

ProcResult Run(const std::vector<std::string>& argv)
{
    ProcResult r;
    if (argv.empty()) { r.output = "empty argv"; return r; }

    int pipefd[2];
    if (pipe(pipefd) != 0) { r.output = "pipe() failed"; return r; }

    // Child writes stdout+stderr into the pipe; both dup'd to the write end.
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, cargv[0], &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);   // parent only reads

    if (rc != 0) {
        close(pipefd[0]);
        r.output = std::string(cargv[0]) + ": " + std::strerror(rc);
        return r;
    }
    r.spawned = true;

    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
        r.output.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* retry */ }
    if      (WIFEXITED(status))   r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exitCode = 128 + WTERMSIG(status);
    return r;
}

} // namespace gl
