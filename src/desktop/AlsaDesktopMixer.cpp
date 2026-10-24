#include "desktop/AlsaDesktopMixer.hpp"

#include <alsa/asoundlib.h>
#include <poll.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace lewitt {

namespace {
constexpr const char* kVolume = "PCM Playback Volume";
constexpr const char* kSwitch = "PCM Playback Switch";

void setId(snd_ctl_elem_id_t* id, const char* name) {
    snd_ctl_elem_id_clear(id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name);
    snd_ctl_elem_id_set_index(id, 0);
}
Status alsaError(const char* what, int err) {
    return makeError(err == -ENODEV ? Errc::Disconnected : Errc::Io, std::string(what) + ": " + snd_strerror(err));
}
}  // namespace

AlsaDesktopMixer::AlsaDesktopMixer(std::string usbId, std::string procAsoundRoot)
    : usbId_(std::move(usbId)), procRoot_(std::move(procAsoundRoot)) {}

AlsaDesktopMixer::~AlsaDesktopMixer() { close(); }

Status AlsaDesktopMixer::open() {
    close();
    auto card = findCardByUsbId(procRoot_, usbId_);
    if (!card) return makeError(Errc::NotFound, "CONNECT 2 ALSA card not found (or not unique)");
    const std::string name = "hw:" + std::to_string(*card);
    snd_ctl_t* ctl = nullptr;
    if (int e = snd_ctl_open(&ctl, name.c_str(), SND_CTL_NONBLOCK); e < 0) return alsaError("open control", e);

    snd_ctl_elem_id_t* id;
    snd_ctl_elem_info_t* info;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    setId(id, kVolume);
    snd_ctl_elem_info_set_id(info, id);
    if (int e = snd_ctl_elem_info(ctl, info); e < 0) {
        snd_ctl_close(ctl);
        return alsaError("PCM Playback Volume (hardware-volume control missing; is the kernel quirk installed?)", e);
    }
    if (snd_ctl_elem_info_get_type(info) != SND_CTL_ELEM_TYPE_INTEGER) {
        snd_ctl_close(ctl);
        return makeError(Errc::NotSupported, "PCM Playback Volume is not an integer control");
    }
    rawMin_ = snd_ctl_elem_info_get_min(info);
    rawMax_ = snd_ctl_elem_info_get_max(info);
    channels_ = snd_ctl_elem_info_get_count(info);
    long dmin = 0, dmax = 0;
    if (int e = snd_ctl_get_dB_range(ctl, id, &dmin, &dmax); e < 0 || dmax <= dmin) {
        snd_ctl_close(ctl);
        return makeError(Errc::NotSupported, "PCM Playback Volume has no usable dB range");
    }
    dbMin_ = dmin / 100.0;
    dbMax_ = dmax / 100.0;
    if (int e = snd_ctl_subscribe_events(ctl, 1); e < 0) {
        snd_ctl_close(ctl);
        return alsaError("subscribe control events", e);
    }
    ctl_ = ctl;
    card_ = *card;
    return okStatus();
}

void AlsaDesktopMixer::close() {
    if (ctl_) snd_ctl_close(ctl_);
    ctl_ = nullptr;
    card_ = -1;
}

std::optional<DesktopLevel> AlsaDesktopMixer::read() {
    if (!ctl_) return std::nullopt;
    snd_ctl_elem_id_t* id;
    snd_ctl_elem_value_t* v;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_value_alloca(&v);
    setId(id, kVolume);
    snd_ctl_elem_value_set_id(v, id);
    if (snd_ctl_elem_read(ctl_, v) < 0) return std::nullopt;
    DesktopLevel d;
    d.db = rawToDb(snd_ctl_elem_value_get_integer(v, 0), rawMin_, rawMax_, dbMin_, dbMax_);
    setId(id, kSwitch);
    snd_ctl_elem_value_clear(v);
    snd_ctl_elem_value_set_id(v, id);
    if (snd_ctl_elem_read(ctl_, v) < 0) return std::nullopt;   // no switch: treat as unknown
    d.muted = snd_ctl_elem_value_get_boolean(v, 0) == 0;
    return d;
}

Status AlsaDesktopMixer::write(const DesktopLevel& d) {
    if (!ctl_) return makeError(Errc::Disconnected, "desktop mixer not open");
    snd_ctl_elem_id_t* id;
    snd_ctl_elem_value_t* v;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_value_alloca(&v);
    setId(id, kVolume);
    snd_ctl_elem_value_set_id(v, id);
    const long raw = dbToRaw(d.db, rawMin_, rawMax_, dbMin_, dbMax_, /*roundDown=*/true);
    for (unsigned c = 0; c < channels_; ++c) snd_ctl_elem_value_set_integer(v, c, raw);
    if (int e = snd_ctl_elem_write(ctl_, v); e < 0) return alsaError("write PCM Playback Volume", e);
    setId(id, kSwitch);
    snd_ctl_elem_value_clear(v);
    snd_ctl_elem_value_set_id(v, id);
    snd_ctl_elem_value_set_boolean(v, 0, d.muted ? 0 : 1);
    if (int e = snd_ctl_elem_write(ctl_, v); e < 0) return alsaError("write PCM Playback Switch", e);
    return okStatus();
}

int AlsaDesktopMixer::waitForEvent(int timeoutMs) {
    if (!ctl_) return -1;
    const int n = snd_ctl_poll_descriptors_count(ctl_);
    if (n <= 0) return -1;
    std::vector<pollfd> fds(static_cast<std::size_t>(n));
    snd_ctl_poll_descriptors(ctl_, fds.data(), static_cast<unsigned>(n));
    const int pr = ::poll(fds.data(), fds.size(), timeoutMs);
    if (pr < 0) return errno == EINTR ? 0 : -1;
    if (pr == 0) return 0;
    unsigned short revents = 0;
    snd_ctl_poll_descriptors_revents(ctl_, fds.data(), static_cast<unsigned>(n), &revents);
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
    snd_ctl_event_t* ev;
    snd_ctl_event_alloca(&ev);
    int watched = 0;
    for (;;) {
        const int r = snd_ctl_read(ctl_, ev);
        if (r == -EAGAIN || r == 0) break;
        if (r < 0) return -1;                                   // -ENODEV: card removed
        if (snd_ctl_event_get_type(ev) != SND_CTL_EVENT_ELEM) continue;
        const unsigned mask = snd_ctl_event_elem_get_mask(ev);
        if (mask == SND_CTL_EVENT_MASK_REMOVE) return -1;
        const char* name = snd_ctl_event_elem_get_name(ev);
        if (name && (!std::strcmp(name, kVolume) || !std::strcmp(name, kSwitch))) watched = 1;
    }
    return watched;
}

}  // namespace lewitt
