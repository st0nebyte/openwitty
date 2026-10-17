#include "ReplayTransport.hpp"

namespace lewitt {

ReplayTransport::ReplayTransport(std::vector<Bytes> settingsFrames, std::vector<Bytes> statusFrames)
    : settings_(std::move(settingsFrames)), status_(std::move(statusFrames)) {}

Status ReplayTransport::open() {
    if (settings_.empty()) return makeError(Errc::NotFound, "replay fixture has no settings snapshot");
    open_ = true;
    return okStatus();
}

Result<Bytes> ReplayTransport::next(std::vector<Bytes>& frames, std::size_t& index, MessageId id) {
    if (!open_) return makeError(Errc::Disconnected, "replay transport is not open");
    if (frames.empty()) return makeError(Errc::NotFound, "replay fixture has no requested message");
    const auto expected = messageInfo(id)->size;
    const Bytes& frame = frames[std::min(index, frames.size() - 1)];
    if (frame.size() != expected)
        return makeError(frame.size() < expected ? Errc::ShortTransfer : Errc::Malformed,
                         "replay fixture message has the wrong size");
    if (index + 1 < frames.size()) ++index;
    return frame;
}

Result<Bytes> ReplayTransport::read(MessageId id) {
    switch (id) {
        case MessageId::ApiVersion: {
            Bytes api(0x40, 0);
            api[0] = 1;
            return api;
        }
        case MessageId::Settings: return next(settings_, settingsIndex_, id);
        case MessageId::Status: return next(status_, statusIndex_, id);
        default: return makeError(Errc::NotSupported, "message not present in replay fixture");
    }
}

}  // namespace lewitt
