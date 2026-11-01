#include "transport/DeviceService.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "protocol/Messages.hpp"
#include "protocol/WriteGuard.hpp"

namespace lewitt {

namespace {
constexpr std::size_t kMaxPayload = 1024;

std::string infoText(const DeviceInfo& i) {
    return std::to_string(i.vid) + "\n" + std::to_string(i.pid) + "\n" + i.product + "\n" + i.serial + "\n" + i.backend;
}
DeviceInfo parseInfo(const std::string& s) {
    DeviceInfo i;
    std::vector<std::string> f;
    std::size_t p = 0;
    for (int k = 0; k < 4; ++k) {
        const auto e = s.find('\n', p);
        if (e == std::string::npos) break;
        f.push_back(s.substr(p, e - p));
        p = e + 1;
    }
    f.push_back(s.substr(std::min(p, s.size())));
    if (f.size() == 5) {
        i.vid = static_cast<std::uint16_t>(std::strtoul(f[0].c_str(), nullptr, 10));
        i.pid = static_cast<std::uint16_t>(std::strtoul(f[1].c_str(), nullptr, 10));
        i.product = f[2];
        i.serial = f[3];
        i.backend = "service";
    }
    return i;
}

// Waits until `fd` is ready (POLLIN or POLLOUT) in 100 ms slices; false on timeout, cancel or error.
bool waitFd(int fd, short events, int timeoutMs, const std::atomic<bool>* cancel, Errc* why) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (cancel && cancel->load()) { *why = Errc::Cancelled; return false; }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
        if (left <= 0) { *why = Errc::Timeout; return false; }
        pollfd p{fd, events, 0};
        const int r = ::poll(&p, 1, static_cast<int>(std::min<long long>(left, 100)));
        if (r > 0) {
            if (p.revents & (POLLERR | POLLNVAL)) { *why = Errc::Disconnected; return false; }
            return true;
        }
        if (r < 0 && errno != EINTR) { *why = Errc::Io; return false; }
    }
}
bool sendAll(int fd, const std::uint8_t* b, std::size_t n, int timeoutMs, const std::atomic<bool>* cancel, Errc* why) {
    while (n > 0) {
        if (!waitFd(fd, POLLOUT, timeoutMs, cancel, why)) return false;
        const ssize_t w = ::send(fd, b, n, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (w <= 0) { *why = Errc::Disconnected; return false; }
        b += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}
bool recvAll(int fd, std::uint8_t* b, std::size_t n, int timeoutMs, const std::atomic<bool>* cancel, Errc* why) {
    while (n > 0) {
        if (!waitFd(fd, POLLIN, timeoutMs, cancel, why)) return false;
        const ssize_t r = ::recv(fd, b, n, MSG_DONTWAIT);
        if (r < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (r <= 0) { *why = Errc::Disconnected; return false; }
        b += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}
Status sendResponse(int fd, Errc code, std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> f{static_cast<std::uint8_t>(code), 0, static_cast<std::uint8_t>(payload.size() & 0xFF),
                                static_cast<std::uint8_t>(payload.size() >> 8)};
    f.insert(f.end(), payload.begin(), payload.end());
    Errc why = Errc::Ok;
    if (!sendAll(fd, f.data(), f.size(), 3000, nullptr, &why)) return makeError(why, "client gone");
    return okStatus();
}
Status sendError(int fd, const Error& e) {
    const std::string m = e.message.substr(0, kMaxPayload);
    return sendResponse(fd, e.code, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(m.data()), m.size()));
}
}  // namespace

std::string deviceServiceSocketPath() {
    if (const char* off = std::getenv("LEWITT_NO_DEVICE_SERVICE"); off && std::string(off) == "1") return {};   // tests
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt || rt[0] != '/') return {};
    return std::string(rt) + "/openwitty/device.sock";
}

// ---- client ---------------------------------------------------------------------------------------------------------

ServiceTransport::ServiceTransport(std::string socketPath, ServiceRole role, int timeoutMs)
    : path_(std::move(socketPath)), role_(role), timeoutMs_(timeoutMs) {}

ServiceTransport::~ServiceTransport() { close(); }

void ServiceTransport::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

Status ServiceTransport::open() {
    close();
    sockaddr_un a{};
    if (path_.empty() || path_.size() >= sizeof a.sun_path) return makeError(Errc::NotSupported, "no OpenWitty service socket");
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return makeError(Errc::NotSupported, "no OpenWitty service socket");
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path_.c_str(), path_.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        ::close(fd);
        return makeError(Errc::NotSupported, "the OpenWitty background service is not running");
    }
    fd_ = fd;
    const std::uint8_t role = static_cast<std::uint8_t>(role_);
    auto r = call(ServiceOp::Hello, 0, std::span<const std::uint8_t>(&role, 1));
    if (!r) { close(); return r.error(); }
    info_ = parseInfo(std::string(r.value().begin(), r.value().end()));
    return okStatus();
}

Result<Bytes> ServiceTransport::call(ServiceOp op, std::uint8_t id, std::span<const std::uint8_t> payload) {
    if (fd_ < 0) return makeError(Errc::Disconnected, "not connected to the OpenWitty service");
    if (cancel_.load()) return makeError(Errc::Cancelled, "request cancelled");
    std::vector<std::uint8_t> f{'O', 'W', kServiceProtocolVersion, static_cast<std::uint8_t>(op), id, 0,
                                static_cast<std::uint8_t>(payload.size() & 0xFF), static_cast<std::uint8_t>(payload.size() >> 8)};
    f.insert(f.end(), payload.begin(), payload.end());
    Errc why = Errc::Ok;
    std::uint8_t h[4];
    if (!sendAll(fd_, f.data(), f.size(), timeoutMs_, &cancel_, &why) || !recvAll(fd_, h, 4, timeoutMs_, &cancel_, &why)) {
        close();   // a half-done exchange leaves the stream unusable
        return makeError(why, why == Errc::Timeout ? "the OpenWitty service did not answer in time"
                              : why == Errc::Cancelled ? "request cancelled" : "connection to the OpenWitty service lost");
    }
    const std::size_t len = h[2] | (h[3] << 8);
    Bytes body(len);
    if (len > kMaxPayload || (len && !recvAll(fd_, body.data(), len, timeoutMs_, &cancel_, &why))) {
        close();
        return makeError(len > kMaxPayload ? Errc::Malformed : why, "invalid answer from the OpenWitty service");
    }
    if (h[0] != 0) {
        const Errc code = h[0] <= static_cast<std::uint8_t>(Errc::Internal) ? static_cast<Errc>(h[0]) : Errc::Internal;
        return makeError(code, std::string(body.begin(), body.end()));
    }
    return body;
}

Result<Bytes> ServiceTransport::read(MessageId id) {
    auto r = call(ServiceOp::Read, static_cast<std::uint8_t>(id), {});
    if (!r) return r;
    const MessageInfo* mi = messageInfo(id);
    if (!mi || r.value().size() != mi->size) return makeError(Errc::ShortTransfer, "wrong message size from the service");
    return r;
}

Status ServiceTransport::write(MessageId id, std::span<const std::uint8_t> data) {
    auto r = call(ServiceOp::Write, static_cast<std::uint8_t>(id), data);
    if (!r) return r.error();
    return okStatus();
}

// ---- service or direct ----------------------------------------------------------------------------------------------

ServiceOrDirectTransport::ServiceOrDirectTransport(std::string socketPath, ServiceRole role,
                                                   std::function<std::unique_ptr<IDeviceTransport>()> direct)
    : service_(std::make_unique<ServiceTransport>(std::move(socketPath), role)), makeDirect_(std::move(direct)) {}

Status ServiceOrDirectTransport::open() {
    if (active_) active_->close();
    auto s = service_->open();
    if (s || s.error().code != Errc::NotSupported) {
        active_ = service_.get();
        return s;
    }
    if (!direct_) direct_ = makeDirect_();
    if (!direct_) return makeError(Errc::NotFound, "no transport");
    if (cancel_.load()) direct_->requestCancel();
    active_ = direct_.get();
    return direct_->open();
}

Result<Bytes> ServiceOrDirectTransport::read(MessageId id) {
    if (!active_) return makeError(Errc::Disconnected, "not open");
    return active_->read(id);
}
Status ServiceOrDirectTransport::write(MessageId id, std::span<const std::uint8_t> data) {
    if (!active_) return makeError(Errc::Disconnected, "not open");
    return active_->write(id, data);
}
void ServiceOrDirectTransport::requestCancel() {
    cancel_.store(true);
    service_->requestCancel();
    if (direct_) direct_->requestCancel();
}
void ServiceOrDirectTransport::clearCancel() {
    cancel_.store(false);
    service_->clearCancel();
    if (direct_) direct_->clearCancel();
}

// ---- server ---------------------------------------------------------------------------------------------------------

DeviceServer::DeviceServer(PolicyFn policy, Logger log, int helloWaitMs)
    : policy_(std::move(policy)), log_(std::move(log)), helloWaitMs_(helloWaitMs) {}

DeviceServer::~DeviceServer() { stop(); }

Status DeviceServer::start(const std::string& socketPath) {
    sockaddr_un a{};
    if (socketPath.empty() || socketPath.size() >= sizeof a.sun_path) return makeError(Errc::InvalidArgument, "no socket path");
    const std::string dir = socketPath.substr(0, socketPath.rfind('/'));
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return makeError(Errc::Io, "cannot create " + dir);
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 077))
        return makeError(Errc::AccessDenied, dir + " is not a private directory of this user");
    // A socket that answers belongs to a running service; one that does not is left over and removed.
    {
        sockaddr_un pa{};
        pa.sun_family = AF_UNIX;
        std::memcpy(pa.sun_path, socketPath.c_str(), socketPath.size() + 1);
        const int pfd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const bool alive = pfd >= 0 && ::connect(pfd, reinterpret_cast<sockaddr*>(&pa), sizeof pa) == 0;
        if (pfd >= 0) ::close(pfd);
        if (alive) return makeError(Errc::ControlInUse, "another OpenWitty service is running");
        ::unlink(socketPath.c_str());
    }
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) return makeError(Errc::Io, "socket failed");
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, socketPath.c_str(), socketPath.size() + 1);
    const mode_t old = ::umask(077);
    const int b = ::bind(listenFd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
    ::umask(old);
    if (b != 0 || ::chmod(socketPath.c_str(), 0600) != 0 || ::listen(listenFd_, 8) != 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return makeError(Errc::Io, "cannot listen on " + socketPath);
    }
    if (::pipe2(wakeFd_, O_CLOEXEC) != 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return makeError(Errc::Io, "pipe failed");
    }
    path_ = socketPath;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return okStatus();
}

void DeviceServer::stop() {
    if (!running_.exchange(false)) return;
    const char c = 0;
    (void)!::write(wakeFd_[1], &c, 1);
    tcv_.notify_all();
    if (thread_.joinable()) thread_.join();
    ::close(listenFd_);
    ::close(wakeFd_[0]);
    ::close(wakeFd_[1]);
    listenFd_ = wakeFd_[0] = wakeFd_[1] = -1;
    ::unlink(path_.c_str());
}

void DeviceServer::setTransport(std::shared_ptr<IDeviceTransport> t) {
    {
        std::lock_guard lk(tm_);
        transport_ = std::move(t);
    }
    tcv_.notify_all();
}

void DeviceServer::loop() {
    std::vector<int> clients;
    auto drop = [&](int fd) {
        if (controllerFd_.load() == fd) {
            controllerFd_ = -1;
            if (log_) log_("service: the controlling client left; the service resumes its own work");
        }
        ::close(fd);
        clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
        clients_ = static_cast<int>(clients.size());
    };
    while (running_) {
        std::vector<pollfd> p{{listenFd_, POLLIN, 0}, {wakeFd_[0], POLLIN, 0}};
        for (int c : clients) p.push_back({c, POLLIN, 0});
        if (::poll(p.data(), p.size(), 1000) < 0 && errno != EINTR) break;
        if (!running_) break;
        if (p[0].revents & POLLIN) {
            const int fd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd >= 0) {
                ucred cr{};
                socklen_t len = sizeof cr;
                if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &len) != 0 || cr.uid != ::geteuid() || clients.size() >= 8) {
                    ::close(fd);
                } else {
                    clients.push_back(fd);
                    clients_ = static_cast<int>(clients.size());
                }
            }
        }
        for (std::size_t i = 2; i < p.size(); ++i)
            if (p[i].revents & (POLLIN | POLLHUP | POLLERR))
                if (!serve(p[i].fd)) drop(p[i].fd);
    }
    for (int c : std::vector<int>(clients)) drop(c);
}

Status DeviceServer::checkWrite(IDeviceTransport& t, MessageId id, std::span<const std::uint8_t> data, bool controller) {
    if (!controller) return makeError(Errc::WritesDisabled, "only the controlling OpenWitty client may write");
    const WritePolicy pol = policy_ ? policy_() : WritePolicy{};
    WriteGuardContext ctx;
    ctx.simulated = t.isSimulated();
    ctx.allowPersistentChanges = pol.allowPersistentChanges;
    if (auto s = checkWriteAllowed(id, data, ctx); !s) return s;
    if (t.isSimulated()) return okStatus();
    if (!pol.allowHardwareWrites) return makeError(Errc::WritesDisabled, "the OpenWitty settings permit no device writes");
    if (id == MessageId::Settings) {
        auto now = t.read(MessageId::Settings);
        if (!now) return now.error();
        std::string bad;
        for (std::size_t off = 0; off < data.size() && off < now.value().size(); ++off)
            if (data[off] != now.value()[off] &&
                std::find(pol.hardwareWriteOffsets.begin(), pol.hardwareWriteOffsets.end(), off) == pol.hardwareWriteOffsets.end())
                bad += (bad.empty() ? "" : ", ") + std::to_string(off);
        if (!bad.empty())
            return makeError(Errc::WritesDisabled, "refused by the service: the write changes settings bytes the OpenWitty "
                                                   "permissions do not cover, or the device changed meanwhile (" + bad + ")");
        return okStatus();
    }
    if (id == MessageId::Triggers) {
        const auto word = static_cast<Trigger>(triggerWordOf(data));
        if (std::find(pol.hardwareTriggers.begin(), pol.hardwareTriggers.end(), word) == pol.hardwareTriggers.end())
            return makeError(Errc::WritesDisabled, "refused by the service: this trigger is not permitted");
        return okStatus();
    }
    return makeError(Errc::NotSupported, "message is not writable");
}

bool DeviceServer::serve(int fd) {
    std::uint8_t h[8];
    Errc why = Errc::Ok;
    if (!recvAll(fd, h, 8, 3000, nullptr, &why)) return false;
    const std::size_t len = h[6] | (h[7] << 8);
    if (h[0] != 'O' || h[1] != 'W' || h[2] != kServiceProtocolVersion || len > kMaxPayload) {
        (void)sendError(fd, Error{Errc::Malformed, "unsupported request (OpenWitty versions differ?)"});
        return false;
    }
    Bytes payload(len);
    if (len && !recvAll(fd, payload.data(), len, 3000, nullptr, &why)) return false;
    const auto op = static_cast<ServiceOp>(h[3]);
    const auto id = static_cast<MessageId>(h[4]);
    const bool controller = controllerFd_.load() == fd;

    std::shared_ptr<IDeviceTransport> t;
    if (op == ServiceOp::Hello) {
        const bool wantsControl = !payload.empty() && payload[0] == static_cast<std::uint8_t>(ServiceRole::Controller);
        if (wantsControl && controllerFd_.load() >= 0 && !controller)
            return sendError(fd, Error{Errc::ControlInUse, "another OpenWitty window or command controls the device"}).ok();
        {
            std::unique_lock lk(tm_);
            helloWaiting_ = true;
            tcv_.wait_for(lk, std::chrono::milliseconds(helloWaitMs_), [&] { return !running_ || (transport_ && transport_->isOpen()); });
            helloWaiting_ = false;
            t = transport_;
        }
        if (!t || !t->isOpen())
            return sendError(fd, Error{Errc::NotFound, "the OpenWitty service has no CONNECT 2 right now"}).ok();
        if (wantsControl) {
            controllerFd_ = fd;
            if (log_) log_("service: a window or command took control; the service pauses its own work");
        }
        const std::string info = infoText(t->info());
        return sendResponse(fd, Errc::Ok, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(info.data()), info.size())).ok();
    }
    {
        std::lock_guard lk(tm_);
        t = transport_;
    }
    if (!t || !t->isOpen()) return sendError(fd, Error{Errc::Disconnected, "the OpenWitty service lost the device"}).ok();
    auto noteLost = [&](Errc c) { if (c == Errc::Disconnected || c == Errc::NotFound) deviceLost_ = true; };
    if (op == ServiceOp::Read) {
        auto r = t->read(id);
        if (!r) { noteLost(r.error().code); return sendError(fd, r.error()).ok(); }
        return sendResponse(fd, Errc::Ok, r.value()).ok();
    }
    if (op == ServiceOp::Write) {
        if (auto c = checkWrite(*t, id, payload, controller); !c) return sendError(fd, c.error()).ok();
        auto w = t->write(id, payload);
        if (!w) { noteLost(w.error().code); return sendError(fd, w.error()).ok(); }
        return sendResponse(fd, Errc::Ok, {}).ok();
    }
    return sendError(fd, Error{Errc::NotSupported, "unknown request"}).ok();
}

}  // namespace lewitt
