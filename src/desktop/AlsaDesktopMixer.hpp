// libasound implementation of IDesktopMixer for the CONNECT 2 card only.
#pragma once
#include <string>

#include "desktop/DesktopMixer.hpp"

struct _snd_ctl;

namespace lewitt {

class AlsaDesktopMixer final : public IDesktopMixer {
public:
    explicit AlsaDesktopMixer(std::string usbId = "29c2:0004", std::string procAsoundRoot = "/proc/asound");
    ~AlsaDesktopMixer() override;
    AlsaDesktopMixer(const AlsaDesktopMixer&) = delete;
    AlsaDesktopMixer& operator=(const AlsaDesktopMixer&) = delete;

    Status open() override;
    void close() override;
    bool isOpen() const override { return ctl_ != nullptr; }
    int cardIndex() const override { return card_; }
    std::optional<DesktopLevel> read() override;
    Status write(const DesktopLevel& d) override;
    int waitForEvent(int timeoutMs) override;

private:
    std::string usbId_, procRoot_;
    _snd_ctl* ctl_{nullptr};
    int card_{-1};
    long rawMin_{0}, rawMax_{0};
    double dbMin_{0}, dbMax_{0};
    unsigned channels_{1};
};

}  // namespace lewitt
