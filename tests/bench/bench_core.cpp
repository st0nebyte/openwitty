// Repeatable micro-benchmarks for the Qt-free core (mock transport; no hardware).
// usage: lewitt_bench [iterations]   -- prints one line per benchmark: name, ns/op
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"
#include "transport/SerializedTransport.hpp"

using namespace lewitt;
using clk = std::chrono::steady_clock;

template <class F>
static void bench(const char* name, long iters, F&& f) {
    for (long i = 0; i < iters / 10 + 1; ++i) f();       // warm-up
    auto t0 = clk::now();
    for (long i = 0; i < iters; ++i) f();
    double ns = std::chrono::duration<double, std::nano>(clk::now() - t0).count() / iters;
    std::printf("%-44s %10.1f ns/op  (%ld iters)\n", name, ns, iters);
}

static volatile double sink;

int main(int argc, char** argv) {
    long N = argc > 1 ? std::atol(argv[1]) : 200000;
    Bytes raw(0x80, 0);
    raw[0] = 0x80; raw[1] = 0x19; raw[20] = 0x00; raw[21] = 0xE2;

    bench("SettingsMessage::fromBytes", N, [&] { auto m = SettingsMessage::fromBytes(raw); sink = m.value().gainDb(0); });
    SettingsMessage s = SettingsMessage::fromBytes(raw).value();
    bench("settings decode (12 accessors)", N, [&] {
        sink = s.gainDb(0) + s.gainDb(1) + s.speakerVolumeDb() + s.headphoneVolumeDb() + s.inputPlaybackMix() +
               s.mute(0) + s.highPass(0) + s.clipguard(0) + s.denoise(1) + s.compress(1) + s.dim() + s.mono();
    });
    bench("settings edit (setGainDb)", N, [&] { (void)s.setGainDb(0, 25.5); });
    SettingsMessage other = s;
    (void)other.setHeadphoneVolumeDb(-10);
    bench("mergeExternalChanges (62-field schema)", N, [&] { SettingsMessage p = s; p.mergeExternalChanges(s, other); sink = p.bytes()[0]; });
    bench("verifyOnlyKnownFieldsChanged", N, [&] { sink = verifyOnlyKnownFieldsChanged(s, other).ok(); });
    Bytes st(0x40, 0);
    bench("StatusMessage::fromBytes + 6 meters", N, [&] {
        auto m = StatusMessage::fromBytes(st).value();
        sink = m.input1LevelDb() + m.input2LevelDb() + m.speakerLevelDb(0) + m.speakerLevelDb(1) + m.headphoneLevelDb(0) + m.loopbackLevelDb(1);
    });

    {   // transport read: raw mock vs serialized decorator
        MockTransport m; (void)m.open();
        bench("MockTransport::read(status)", N, [&] { sink = m.read(MessageId::Status).ok(); });
        SerializedTransport t(std::make_unique<MockTransport>()); (void)t.open();
        bench("SerializedTransport::read(status)", N, [&] { sink = t.read(MessageId::Status).ok(); });
    }
    {   // a full poll tick as the GUI worker does it (status read + merge-aware settings refresh)
        auto m = std::make_unique<MockTransport>();
        DeviceSession sess(std::make_unique<SerializedTransport>(std::move(m)));
        (void)sess.connect();
        bench("session tick: refreshStatus", N / 2, [&] { sink = sess.refreshStatus().ok(); });
        bench("session tick: refreshSettings (merge)", N / 2, [&] { sink = sess.refreshSettings().ok(); });
        bench("edit + commit (write + re-read)", N / 4, [&] {
            (void)sess.edit([](SettingsMessage& x) { return x.setGainDb(0, 10); });
            (void)sess.commit();
            (void)sess.edit([](SettingsMessage& x) { return x.setGainDb(0, 11); });
            (void)sess.commit();
        });
    }
    return 0;
}
