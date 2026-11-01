// Single device owner (2026-11-01): the background service (openwitty-volumed) keeps the CONNECT 2 open and is the only
// process that talks to it over USB; the window and the CLI reach the device through the service's local socket.
// Without the service they open the device themselves as before (ServiceOrDirectTransport).
//
// The socket ($XDG_RUNTIME_DIR/openwitty/device.sock, mode 0600, same user only via SO_PEERCRED) carries the transport
// calls 1:1 - message reads and writes - so every check of the client's DeviceSession (policy, write verification,
// read-back) stays where it is. The service checks every write again against the user's permissions: on real
// hardware a settings write may change only permitted bytes compared with a fresh read, a trigger only a permitted
// word, and only the one "controller" client (the window, or a CLI write) may write. While a controller is connected
// the service pauses its own desktop link and follower (the controller runs its own, as before), but keeps the device.
//
// Wire format, little endian. Request: 'O' 'W' version op msgId 0 len16 payload. Response: status(Errc) 0 len16
// payload (bytes, device info, or an error message).
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/DeviceSession.hpp"
#include "transport/IDeviceTransport.hpp"

namespace lewitt {

inline constexpr std::uint8_t kServiceProtocolVersion = 1;
enum class ServiceOp : std::uint8_t { Hello = 1, Read = 2, Write = 3 };
enum class ServiceRole : std::uint8_t { Reader = 0, Controller = 1 };

// $XDG_RUNTIME_DIR/openwitty/device.sock; empty without XDG_RUNTIME_DIR or with LEWITT_NO_DEVICE_SERVICE=1 (tests).
std::string deviceServiceSocketPath();

// Client side: one connection, one request at a time.
class ServiceTransport final : public IDeviceTransport {
public:
    explicit ServiceTransport(std::string socketPath, ServiceRole role, int timeoutMs = 4000);
    ~ServiceTransport() override;
    std::string name() const override { return "service"; }
    // Errc::NotSupported when no service listens (callers fall back to USB); other errors come from the service.
    Status open() override;
    void close() override;
    bool isOpen() const override { return fd_ >= 0; }
    DeviceInfo info() const override { return info_; }
    Result<Bytes> read(MessageId id) override;
    Status write(MessageId id, std::span<const std::uint8_t> data) override;
    void requestCancel() override { cancel_.store(true); }
    void clearCancel() override { cancel_.store(false); }

private:
    std::string path_;
    ServiceRole role_;
    int timeoutMs_;
    int fd_{-1};
    DeviceInfo info_;
    std::atomic<bool> cancel_{false};
    Result<Bytes> call(ServiceOp op, std::uint8_t id, std::span<const std::uint8_t> payload);
};

// The service when it is running, otherwise the direct transport (made on demand by `direct`).
class ServiceOrDirectTransport final : public IDeviceTransport {
public:
    ServiceOrDirectTransport(std::string socketPath, ServiceRole role,
                             std::function<std::unique_ptr<IDeviceTransport>()> direct);
    std::string name() const override { return active_ ? active_->name() : "service-or-usb"; }
    Status open() override;
    void close() override { if (active_) active_->close(); }
    bool isOpen() const override { return active_ && active_->isOpen(); }
    DeviceInfo info() const override { return active_ ? active_->info() : DeviceInfo{}; }
    Result<Bytes> read(MessageId id) override;
    Status write(MessageId id, std::span<const std::uint8_t> data) override;
    void requestCancel() override;
    void clearCancel() override;
    bool viaService() const { return active_ == service_.get(); }

private:
    std::unique_ptr<ServiceTransport> service_;
    std::function<std::unique_ptr<IDeviceTransport>()> makeDirect_;
    std::unique_ptr<IDeviceTransport> direct_;
    IDeviceTransport* active_{nullptr};
    std::atomic<bool> cancel_{false};
};

// Forwards to a transport shared with the service's socket server (the session owns only this handle).
class SharedTransportHandle final : public IDeviceTransport {
public:
    explicit SharedTransportHandle(std::shared_ptr<IDeviceTransport> t) : t_(std::move(t)) {}
    std::string name() const override { return t_->name(); }
    Status open() override { return t_->open(); }
    void close() override { t_->close(); }
    bool isOpen() const override { return t_->isOpen(); }
    DeviceInfo info() const override { return t_->info(); }
    Result<Bytes> read(MessageId id) override { return t_->read(id); }
    Status write(MessageId id, std::span<const std::uint8_t> d) override { return t_->write(id, d); }
    bool isSimulated() const override { return t_->isSimulated(); }
    void requestCancel() override { t_->requestCancel(); }
    void clearCancel() override { t_->clearCancel(); }

private:
    std::shared_ptr<IDeviceTransport> t_;
};

// Service side: accepts clients and runs their calls on the shared transport (one at a time).
class DeviceServer {
public:
    using PolicyFn = std::function<WritePolicy()>;
    using Logger = std::function<void(const std::string&)>;
    DeviceServer(PolicyFn policy, Logger log = {}, int helloWaitMs = 3000);
    ~DeviceServer();
    // Creates the socket (directory 0700, socket 0600) and starts the server thread.
    Status start(const std::string& socketPath);
    void stop();
    // The device the service holds now (nullptr: none). A Hello waits up to 3 s for one.
    void setTransport(std::shared_ptr<IDeviceTransport> t);
    bool controllerConnected() const { return controllerFd_.load() >= 0; }
    // A client is waiting for the device or connected (the service opens the device for it even when idle).
    bool clientsWaiting() const { return clients_.load() > 0 || helloWaiting_.load(); }
    // A client's request found the device gone (unplugged): the service reopens it. Reading clears the flag.
    bool takeDeviceLost() { return deviceLost_.exchange(false); }

private:
    PolicyFn policy_;
    Logger log_;
    std::string path_;
    int listenFd_{-1};
    int wakeFd_[2]{-1, -1};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> controllerFd_{-1};
    std::atomic<int> clients_{0};
    std::atomic<bool> helloWaiting_{false};
    std::atomic<bool> deviceLost_{false};
    int helloWaitMs_;
    std::mutex tm_;
    std::condition_variable tcv_;
    std::shared_ptr<IDeviceTransport> transport_;
    void loop();
    // Handles one request of client `fd`; false = close the connection.
    bool serve(int fd);
    Status checkWrite(IDeviceTransport& t, MessageId id, std::span<const std::uint8_t> data, bool controller);
};

}  // namespace lewitt
