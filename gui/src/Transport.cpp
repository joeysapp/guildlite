#include "Transport.h"
#include "Proc.h"

#include <unistd.h>

#include <utility>

namespace gl {
namespace {

std::string OneLine(std::string s)   // control payloads are multi-line; flatten for the log
{
    for (char& c : s) if (c == '\n') c = '|';
    while (!s.empty() && (s.back() == '|' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string TrimTail(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// -----------------------------------------------------------------------------
class MockTransport final : public ITransport {
public:
    const char* name() const override { return "mock"; }
    SendResult send(const std::string& payload) override
    {
        return {true, "[mock] would send -> [" + OneLine(payload) + "]"};
    }
    SendResult fetchLog() override { return {true, "[mock] no remote log in mock mode"}; }
};

// -----------------------------------------------------------------------------
class SshTransport final : public ITransport {
public:
    explicit SshTransport(Target t) : t_(std::move(t)) {}
    const char* name() const override { return "ssh"; }

    SendResult send(const std::string& payload) override
    {
        // Write the payload to a temp file, then scp it to the control path -- exactly the
        // proven one-liner (scp -q /tmp/gl-control host:Documents/guildlite/control), just
        // driven from the GUI. scp needs no remote shell, so it is cmd/powershell-agnostic.
        char tmpl[] = "/tmp/gl-control-XXXXXX";
        const int fd = mkstemp(tmpl);
        if (fd < 0) return {false, "could not create temp file"};
        const ssize_t wrote = ::write(fd, payload.data(), payload.size());
        ::close(fd);
        if (wrote < 0) { ::unlink(tmpl); return {false, "temp write failed"}; }

        const std::string timeout = std::to_string(t_.connectTimeout);
        const ProcResult r = Run({"scp", "-q",
                                  "-o", "BatchMode=yes",
                                  "-o", "ConnectTimeout=" + timeout,
                                  tmpl, t_.host + ":" + t_.controlPath});
        ::unlink(tmpl);

        if (!r.spawned)      return {false, "scp not found? " + TrimTail(r.output)};
        if (r.exitCode != 0) return {false, "scp exit " + std::to_string(r.exitCode) +
                                            (r.output.empty() ? " (host unreachable?)"
                                                              : ": " + TrimTail(r.output))};
        return {true, "sent -> " + t_.host + ":" + t_.controlPath + "  [" + OneLine(payload) + "]"};
    }

    SendResult fetchLog() override
    {
        const std::string timeout = std::to_string(t_.connectTimeout);
        const ProcResult r = Run({"ssh",
                                  "-o", "BatchMode=yes",
                                  "-o", "ConnectTimeout=" + timeout,
                                  t_.host, t_.logCommand});
        if (!r.spawned) return {false, "ssh not found? " + TrimTail(r.output)};
        if (r.exitCode != 0 && r.output.empty())
            return {false, "ssh exit " + std::to_string(r.exitCode) + " (host unreachable?)"};
        return {true, r.output.empty() ? "(no log output)" : TrimTail(r.output)};
    }

private:
    Target t_;
};

} // namespace

std::unique_ptr<ITransport> MakeMockTransport()        { return std::make_unique<MockTransport>(); }
std::unique_ptr<ITransport> MakeSshTransport(Target t) { return std::make_unique<SshTransport>(std::move(t)); }

} // namespace gl
