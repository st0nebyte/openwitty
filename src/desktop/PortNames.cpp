#include "desktop/PortNames.hpp"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <regex>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace lewitt {

std::vector<PwObject> parsePwCliList(const std::string& text) {
    static const std::regex head(R"(^\s*id (\d+), type )");
    static const std::regex prop(R"(^\s*([A-Za-z0-9_.\-]+) = \"(.*)\"\s*$)");
    std::vector<PwObject> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        std::smatch m;
        if (std::regex_search(line, m, head)) {
            const std::string id = m[1].str();
            if (id.size() > 9) continue;
            out.push_back(PwObject{std::stoi(id), {}});
        } else if (!out.empty() && std::regex_match(line, m, prop)) {
            out.back().props[m[1].str()] = m[2].str();
        }
    }
    return out;
}

std::map<int, std::string> parsePrettyNames(const std::string& text) {
    static const std::regex line(R"(update: id:(\d+) key:'http://jackaudio\.org/metadata/pretty-name' value:'([^']*)')");
    std::map<int, std::string> out;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), line); it != std::sregex_iterator(); ++it)
        if ((*it)[1].str().size() <= 9) out[std::stoi((*it)[1].str())] = (*it)[2].str();
    return out;
}

std::vector<PortName> wantedPortNames(const std::vector<PwObject>& nodes, const std::vector<PwObject>& ports) {
    static const std::map<std::string, std::string> capture{
        {"FL", "1 Microphone"}, {"FR", "2 Instrument"}, {"RL", "3 Loopback L"}, {"RR", "4 Loopback R"}};
    static const std::map<std::string, std::string> playback{{"FL", "Output L"}, {"FR", "Output R"}};
    std::vector<PortName> out;
    for (const auto& n : nodes) {
        auto it = n.props.find("node.name");
        if (it == n.props.end()) continue;
        const bool in = it->second.rfind("alsa_input.usb-Lewitt_GmbH_CONNECT_2", 0) == 0;
        const bool outp = it->second.rfind("alsa_output.usb-Lewitt_GmbH_CONNECT_2", 0) == 0;
        if (!in && !outp) continue;
        const auto& names = in ? capture : playback;
        const std::string dir = in ? "out" : "in";   // a source's ports are outputs, a sink's ports inputs
        std::vector<PortName> mine;
        std::map<std::string, int> seen;
        for (const auto& p : ports) {
            auto nid = p.props.find("node.id");
            if (nid == p.props.end() || nid->second != std::to_string(n.id)) continue;
            auto d = p.props.find("port.direction");
            auto mon = p.props.find("port.monitor");
            if (d == p.props.end() || d->second != dir || (mon != p.props.end() && mon->second == "true")) continue;
            auto ch = p.props.find("audio.channel");
            if (ch == p.props.end()) continue;
            ++seen[ch->second];
            if (auto nm = names.find(ch->second); nm != names.end()) mine.push_back({p.id, nm->second});
        }
        // exactly the expected positions, each once; otherwise this is another profile: leave it alone
        bool exact = seen.size() == names.size();
        for (const auto& [ch, count] : seen) exact = exact && count == 1 && names.count(ch);
        if (exact) out.insert(out.end(), mine.begin(), mine.end());
    }
    return out;
}

std::vector<PortName> missingPortNames(const std::vector<PortName>& wanted, const std::map<int, std::string>& current) {
    std::vector<PortName> out;
    for (const auto& w : wanted) {
        auto it = current.find(w.portId);
        if (it == current.end() || it->second != w.name) out.push_back(w);
    }
    return out;
}

int runProgram(const std::vector<std::string>& argv, std::string* out) {
    if (argv.empty()) return -1;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) { close(fds[0]); return -1; }
    std::string data;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0)
        if (data.size() < 4 * 1024 * 1024) data.append(buf, static_cast<std::size_t>(n));
    close(fds[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (out) *out = std::move(data);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int applyPortNames(const CommandRunner& run) {
    std::string nodes, ports, meta;
    if (run({"pw-cli", "ls", "Node"}, &nodes) != 0 || run({"pw-cli", "ls", "Port"}, &ports) != 0 ||
        run({"pw-metadata"}, &meta) != 0)
        return -1;
    const auto missing = missingPortNames(wantedPortNames(parsePwCliList(nodes), parsePwCliList(ports)),
                                          parsePrettyNames(meta));
    int set = 0;
    for (const auto& m : missing)
        if (run({"pw-metadata", std::to_string(m.portId), kPrettyNameKey, m.name}, nullptr) == 0) ++set;
    return set;
}

int clearPortNames(const CommandRunner& run) {
    std::string nodes, ports, meta;
    if (run({"pw-cli", "ls", "Node"}, &nodes) != 0 || run({"pw-cli", "ls", "Port"}, &ports) != 0 ||
        run({"pw-metadata"}, &meta) != 0)
        return -1;
    const auto current = parsePrettyNames(meta);
    int cleared = 0;
    for (const auto& w : wantedPortNames(parsePwCliList(nodes), parsePwCliList(ports))) {
        auto it = current.find(w.portId);
        if (it != current.end() && it->second == w.name &&
            run({"pw-metadata", "-d", std::to_string(w.portId), kPrettyNameKey}, nullptr) == 0)
            ++cleared;
    }
    return cleared;
}

std::optional<unsigned> parseClockRate(const std::string& text) {
    static const std::regex line(R"(key:'clock\.rate' value:'(\d{4,6})')");
    std::smatch m;
    if (!std::regex_search(text, m, line)) return std::nullopt;
    const unsigned hz = static_cast<unsigned>(std::stoul(m[1].str()));
    if (hz < 8000 || hz > 384000) return std::nullopt;
    return hz;
}

}  // namespace lewitt
