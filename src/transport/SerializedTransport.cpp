#include "SerializedTransport.hpp"

namespace lewitt {

SerializedTransport::SerializedTransport(std::unique_ptr<IDeviceTransport> inner) : inner_(std::move(inner)) {}

Status SerializedTransport::open() {
    std::lock_guard lk(m_);
    if (cancelRequested()) return makeError(Errc::Cancelled, "open cancelled");
    return inner_->open();
}

void SerializedTransport::close() {
    std::lock_guard lk(m_);   // an in-flight request finishes first (bounded by its timeout)
    inner_->close();
}

bool SerializedTransport::isOpen() const {
    std::lock_guard lk(m_);
    return inner_->isOpen();
}

void SerializedTransport::requestCancel() {
    cancel_.store(true, std::memory_order_release);
    inner_->requestCancel();   // lets a blocked backend wake up early where it can
}

void SerializedTransport::clearCancel() {
    cancel_.store(false, std::memory_order_release);
    inner_->clearCancel();
}

void SerializedTransport::record(bool isWrite, bool ok, Errc code, std::chrono::steady_clock::duration d) {
    std::lock_guard lk(statsM_);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(d);
    (isWrite ? stats_.writes : stats_.reads)++;
    if (!ok) { stats_.errors++; if (code == Errc::Cancelled) stats_.cancelled++; }
    stats_.totalLatency += us;
    if (us > stats_.maxLatency) stats_.maxLatency = us;
}

Result<Bytes> SerializedTransport::read(MessageId id) {
    std::lock_guard lk(m_);
    if (cancelRequested()) { record(false, false, Errc::Cancelled, {}); return makeError(Errc::Cancelled, "request cancelled"); }
    auto t0 = std::chrono::steady_clock::now();
    auto r = inner_->read(id);
    record(false, r.ok(), r.ok() ? Errc::Ok : r.error().code, std::chrono::steady_clock::now() - t0);
    if (r && cancelRequested()) return makeError(Errc::Cancelled, "request cancelled");
    return r;
}

Status SerializedTransport::write(MessageId id, std::span<const std::uint8_t> data) {
    std::lock_guard lk(m_);
    if (cancelRequested()) { record(true, false, Errc::Cancelled, {}); return makeError(Errc::Cancelled, "request cancelled"); }
    auto t0 = std::chrono::steady_clock::now();
    auto r = inner_->write(id, data);
    record(true, r.ok(), r.ok() ? Errc::Ok : r.error().code, std::chrono::steady_clock::now() - t0);
    return r;
}

TransportStats SerializedTransport::stats() const {
    std::lock_guard lk(statsM_);
    return stats_;
}

}  // namespace lewitt
