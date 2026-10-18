// Bounded, read-only CONNECT 2 status sampler. --execute is mandatory for real USB.
// No USB write API is referenced here; the transport also has allowWrites=false.
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <time.h>

#include "protocol/Messages.hpp"
#include "transport/MockTransport.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
using Clock = std::chrono::steady_clock;
constexpr int kMaxStatusGets = 80;
constexpr std::array<std::size_t, 8> kMeterOffsets{0x00, 0x09, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c};

bool parseInt(std::string_view text, int& value) {
    const auto* end = text.data() + text.size();
    auto result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

double cpuMs() {
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

bool sameMeterBytes(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    for (auto offset : kMeterOffsets)
        if (a[offset] != b[offset] || a[offset + 1] != b[offset + 1]) return false;
    return true;
}

void usage() {
    std::puts("usage: openwitty-meter-probe --rate 4|8 --duration 1..10 [--mock | --execute]\n"
              "Real USB is DRY RUN unless --execute is present. Maximum 80 status GETs plus\n"
              "one API-version GET; stop on the first error. No settings GET or USB SET.\n"
              "Only run --execute after the device owner approves the exact experiment.");
}
} // namespace

int main(int argc, char** argv) {
    int rate = 0, seconds = 0;
    bool execute = false, mock = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help") { usage(); return 0; }
        if (arg == "--rate" && i + 1 < argc) {
            if (!parseInt(argv[++i], rate)) { usage(); return 64; }
        } else if (arg == "--duration" && i + 1 < argc) {
            if (!parseInt(argv[++i], seconds)) { usage(); return 64; }
        } else if (arg == "--execute") execute = true;
        else if (arg == "--mock") mock = true;
        else { usage(); return 64; }
    }
    if ((rate != 4 && rate != 8) || seconds < 1 || seconds > 10 || (mock && execute)) {
        usage(); return 64;
    }
    const int planned = std::min(rate * seconds, kMaxStatusGets);
    std::printf("plan: %d Hz for %d s, at most %d status GETs + 1 API GET, "
                "64-byte status payload, no settings GET, no writes\n", rate, seconds, planned);
    if (!mock && !execute) { std::puts("dry run: no device opened; pass --execute only after approval"); return 0; }

    std::unique_ptr<IDeviceTransport> transport;
    if (mock) transport = std::make_unique<MockTransport>();
    else {
        auto options = readOnlyHardwareOptions();
        options.pid = 0x0004;             // this trial is for CONNECT 2 normal application mode only
        options.readDeviceStrings = false; // no serial/product descriptor GET or storage
        transport = std::make_unique<UsbTransport>(options);
    }
    if (auto opened = transport->open(); !opened) {
        std::fprintf(stderr, "open failed [%s]: %s\n", errcName(opened.error().code), opened.error().message.c_str());
        return 1;
    }
    const auto device = transport->info();
    if (device.vid != kLewittVendorId || deviceModeFromPid(device.pid) == DeviceMode::Unknown) {
        std::fputs("refused: not a known CONNECT 2 application-mode VID/PID\n", stderr);
        return 1;
    }
    std::printf("source=%s vid:pid=%04x:%04x; auto-selected vendor control interface; "
                "serial/product not recorded\n", mock ? "mock" : "usb", device.vid, device.pid);
    auto apiBytes = transport->read(MessageId::ApiVersion);
    if (!apiBytes) {
        std::fprintf(stderr, "API GET failed [%s]: %s\n", errcName(apiBytes.error().code), apiBytes.error().message.c_str());
        return 1;
    }
    auto api = ApiVersionMessage::fromBytes(apiBytes.value());
    if (!api || api.value().major() != kSupportedApiMajor) {
        std::fputs("refused: unsupported or malformed API version\n", stderr);
        return 1;
    }

    const auto period = std::chrono::nanoseconds(1'000'000'000LL / rate);
    const auto began = Clock::now();
    const double cpuBeganMs = cpuMs();
    auto deadline = began + std::chrono::seconds(seconds);
    auto next = began;
    Bytes previous;
    std::array<double, 8> minima{}, maxima{};
    minima.fill(0.0);
    maxima.fill(-60.0);
    int success = 0, errors = 0, meterChanges = 0, statusChanges = 0, repeated = 0, maxRepeatRun = 0, repeatRun = 0;
    double transferTotalMs = 0.0, transferMaxMs = 0.0;
    std::array<double, kMaxStatusGets> getDurationsMs{};
    Clock::time_point firstStarted{}, lastStarted{};
    std::puts("sample,t_ms,get_ms,meter_changed,status_changed,repeat_run,"
              "mic,inst,loop_l,loop_r,spk_l,spk_r,hp_l,hp_r");
    for (int attempt = 0; attempt < planned; ++attempt) {
        std::this_thread::sleep_until(next);
        const auto started = Clock::now();
        if (started >= deadline) break;
        if (success == 0) firstStarted = started;
        lastStarted = started;
        auto response = transport->read(MessageId::Status);
        const auto ended = Clock::now();
        const double getMs = std::chrono::duration<double, std::milli>(ended - started).count();
        if (!response) {
            ++errors;
            std::fprintf(stderr, "STOP: status GET %d failed [%s]: %s\n", attempt + 1,
                         errcName(response.error().code), response.error().message.c_str());
            break; // First error is a stricter stop than repeated-error tolerance.
        }
        auto decoded = StatusMessage::fromBytes(response.value());
        if (!decoded) {
            ++errors;
            std::fprintf(stderr, "STOP: invalid status response [%s]\n", errcName(decoded.error().code));
            break;
        }
        const auto& st = decoded.value();
        const std::array<double, 8> levels{st.input1LevelDb(), st.input2LevelDb(),
            st.loopbackLevelDb(0), st.loopbackLevelDb(1), st.speakerLevelDb(0),
            st.speakerLevelDb(1), st.headphoneLevelDb(0), st.headphoneLevelDb(1)};
        const bool hasPrevious = !previous.empty();
        const bool meterChanged = hasPrevious && !sameMeterBytes(previous, response.value());
        const bool statusChanged = hasPrevious && previous != response.value();
        if (hasPrevious) {
            meterChanges += meterChanged;
            statusChanges += statusChanged;
            if (!meterChanged) { ++repeated; ++repeatRun; }
            else repeatRun = 0;
            maxRepeatRun = std::max(maxRepeatRun, repeatRun);
        }
        for (std::size_t i = 0; i < levels.size(); ++i) {
            minima[i] = success == 0 ? levels[i] : std::min(minima[i], levels[i]);
            maxima[i] = success == 0 ? levels[i] : std::max(maxima[i], levels[i]);
        }
        ++success;
        getDurationsMs[success - 1] = getMs;
        transferTotalMs += getMs;
        transferMaxMs = std::max(transferMaxMs, getMs);
        const double tMs = std::chrono::duration<double, std::milli>(started - began).count();
        std::printf("%d,%.3f,%.3f,%s,%s,%d", success, tMs, getMs,
                    !hasPrevious ? "initial" : meterChanged ? "yes" : "no",
                    !hasPrevious ? "initial" : statusChanged ? "yes" : "no", repeatRun);
        for (double level : levels) std::printf(",%.3f", level);
        std::putchar('\n');
        previous = std::move(response.value());
        next += period;
        if (next <= ended) next = ended + period; // never catch up with a request burst
    }
    const double wallMs = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
    const double usedCpuMs = cpuMs() - cpuBeganMs;
    std::sort(getDurationsMs.begin(), getDurationsMs.begin() + success);
    const double medianGetMs = success ? getDurationsMs[(success - 1) / 2] : 0.0;
    const double p95GetMs = success ? getDurationsMs[(95 * success + 99) / 100 - 1] : 0.0;
    const double observedSpanMs = success > 1 ?
        std::chrono::duration<double, std::milli>(lastStarted - firstStarted).count() : 0.0;
    const double intervalHz = observedSpanMs > 0 ? (success - 1) * 1000.0 / observedSpanMs : 0.0;
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::printf("summary: status_gets=%d errors=%d elapsed_ms=%.1f interval_hz=%.3f "
                "get_mean_ms=%.3f get_median_ms=%.3f get_p95_ms=%.3f get_max_ms=%.3f meter_changed=%d/%d "
                "status_changed=%d/%d identical_meter=%d max_repeat_run=%d "
                "cpu_ms=%.1f cpu_pct=%.2f maxrss_kib=%ld\n",
                success, errors, wallMs, intervalHz,
                success ? transferTotalMs / success : 0.0, medianGetMs, p95GetMs, transferMaxMs,
                meterChanges, std::max(0, success - 1), statusChanges, std::max(0, success - 1),
                repeated, maxRepeatRun, usedCpuMs, wallMs > 0 ? usedCpuMs / wallMs * 100.0 : 0.0,
                usage.ru_maxrss);
    for (std::size_t i = 0; i < minima.size(); ++i)
        std::printf("range[%zu]=%.3f..%.3f dB\n", i, minima[i], maxima[i]);
    transport->close();
    return errors == 0 && success == planned ? 0 : 1;
}
