// Test helper: a MockTransport that claims to be real hardware, so DeviceSession applies the
// real-hardware write policy (allowHardwareWrites + hardwareWriteOffsets). Simulation only.
#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

#include "core/DeviceSession.hpp"
#include "transport/ControlLease.hpp"
#include "transport/MockTransport.hpp"

namespace lewitt::testing {

class FakeHardware final : public IDeviceTransport {
public:
    explicit FakeHardware(MockTransport* inner) : m_(inner) {}
    std::string name() const override { return "fake-hardware"; }
    Status open() override {
        ++openCalls;
        if (useLease && !lease_) {          // like UsbTransport: the per-user control lease first
            auto l = ControlLease::acquireForCurrentSession();
            if (!l) return l.error();
            lease_.emplace(std::move(l.value()));
        }
        return m_->open();
    }
    void close() override {
        m_->close();
        lease_.reset();
    }
    bool isOpen() const override { return m_->isOpen(); }
    DeviceInfo info() const override { return m_->info(); }
    Result<Bytes> read(MessageId id) override {
        ++readCalls;
        return m_->read(id);
    }
    Status write(MessageId id, std::span<const std::uint8_t> d) override {
        ++writeCalls;
        if (dropWrites) return okStatus();     // device acknowledges but ignores (like a stubbed SET)
        return m_->write(id, d);
    }
    bool isSimulated() const override { return false; }
    void requestCancel() override { m_->requestCancel(); }
    void clearCancel() override { m_->clearCancel(); }
    bool dropWrites{false};
    int writeCalls{0};
    int readCalls{0};
    int openCalls{0};
    // Opt-in: hold the real ControlLease (in the test's private XDG_RUNTIME_DIR) while open, as UsbTransport does.
    bool useLease{false};
    bool holdsLease() const { return lease_.has_value(); }

private:
    MockTransport* m_;
    std::optional<ControlLease> lease_;
};

inline PollPolicy fastPoll() {
    PollPolicy pp;
    pp.sleeper = [](std::chrono::milliseconds) {};
    return pp;
}

inline int settingsWrites(const MockTransport& m) {
    auto l = m.writeLog();
    return static_cast<int>(std::count(l.begin(), l.end(), MessageId::Settings));
}

}  // namespace lewitt::testing
