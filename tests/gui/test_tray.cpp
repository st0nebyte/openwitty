// Tray companion, single-instance channel, GUI activation and tray autostart (offscreen; a fake systemctl;
// private config and runtime directories). Nothing here starts a real process, a real service or touches
// a device.
#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <future>
#include <thread>

#include "GuiActivation.hpp"
#include "InstanceChannel.hpp"
#include "IntegrationSettings.hpp"
#include "ServiceState.hpp"
#include "TrayAutostart.hpp"
#include "TrayController.hpp"
#include "TrayModel.hpp"
#include "WindowPlacement.hpp"
#include "core/UserConfig.hpp"

namespace {
QApplication* app() {
    static QTemporaryDir* configHome = [] {
        auto* d = new QTemporaryDir();
        qputenv("XDG_CONFIG_HOME", d->path().toUtf8());
        qunsetenv("XDG_CURRENT_DESKTOP");   // window placement: "unsupported" unless a test chooses a mode
        return d;
    }();
    static QTemporaryDir* runtime = [] {
        auto* d = new QTemporaryDir();
        qputenv("XDG_RUNTIME_DIR", d->path().toUtf8());
        return d;
    }();
    (void)configHome;
    (void)runtime;
    static int argc = 1;
    static char name[] = "lewitt_tray_tests";
    static char* argv[] = {name, nullptr};
    static QApplication* a = new QApplication(argc, argv);
    return a;
}

struct FakeSystemctl {
    QString load{"loaded"}, unitFile{"disabled"}, active{"inactive"};
    int rc{0};
    QList<QStringList> calls;   // program + args
    CommandRunner runner() {
        return [this](const QString& program, const QStringList& args, QString* out) {
            calls.append(QStringList{program} + args);
            if (program == "systemctl" && args.value(1) == "show" && out)
                *out = QString("LoadState=%1\nUnitFileState=%2\nActiveState=%3\n").arg(load, unitFile, active);
            return program == "systemctl" ? rc : 0;
        };
    }
    // anything that would change the service
    bool changedService() const {
        for (const auto& c : calls)
            for (const char* verb : {"enable", "disable", "start", "stop", "restart", "kill", "mask"})
                if (c.value(0) == "systemctl" && c.contains(verb)) return true;
        return false;
    }
};

void writeSync(bool mirror) {
    lewitt::UserConfig c;
    c.desktopVolumeSync = mirror ? lewitt::DesktopVolumeSync::Mirror : lewitt::DesktopVolumeSync::Off;
    ASSERT_TRUE(lewitt::saveUserConfig(c, lewitt::userConfigPath()).ok());
}

template <class F>
auto inThreadWhileProcessingEvents(F f) {
    auto fut = std::async(std::launch::async, f);
    QElapsedTimer t;
    t.start();
    while (fut.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready && t.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return fut.get();
}
}  // namespace

// ---- service state and tray status -------------------------------------------------------------------

TEST(ServiceState, ParsesSystemctlShow) {
    EXPECT_EQ(parseServiceState(0, "LoadState=loaded\nUnitFileState=enabled\nActiveState=active\n"), ServiceState::Active);
    EXPECT_EQ(parseServiceState(0, "LoadState=loaded\nUnitFileState=enabled\nActiveState=inactive\n"), ServiceState::Enabled);
    EXPECT_EQ(parseServiceState(0, "LoadState=loaded\nUnitFileState=disabled\nActiveState=inactive\n"), ServiceState::Disabled);
    EXPECT_EQ(parseServiceState(0, "LoadState=not-found\nUnitFileState=\nActiveState=inactive\n"), ServiceState::NotInstalled);
    EXPECT_EQ(parseServiceState(1, ""), ServiceState::Unknown);
}

TEST(TrayModel, ServiceDisabledAndKnobDisplayOff) {
    app();
    QFile::remove(QString::fromStdString(lewitt::userConfigPath()));
    FakeSystemctl sc;
    TrayModel m(sc.runner());
    m.refresh();
    EXPECT_FALSE(m.knobSync());
    EXPECT_EQ(m.knobLine(), "Desktop volume does not follow the CONNECT 2 knob");
    EXPECT_EQ(m.serviceLine(), "Works only while the window is open");
    EXPECT_TRUE(m.toolTip().startsWith("OpenWitty\n"));
    EXPECT_FALSE(sc.changedService());
}

TEST(TrayModel, ServiceRunningWithKnobDisplayOn) {
    app();
    writeSync(true);
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    sc.active = "active";
    TrayModel m(sc.runner());
    m.refresh();
    EXPECT_EQ(m.knobLine(), "Desktop volume follows the CONNECT 2 knob");
    EXPECT_EQ(m.serviceLine(), "Keeps working when the window is closed");
}

TEST(TrayModel, KnobDisplayOffWhileTheServiceStaysEnabledShowsIdle) {
    app();
    writeSync(false);
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    sc.active = "active";
    TrayModel m(sc.runner());
    m.refresh();
    EXPECT_EQ(m.serviceLine(), "Runs in the background, but has nothing to do");
}

TEST(TrayModel, OtherServiceStatesAndChangeNotification) {
    app();
    writeSync(true);
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    TrayModel m(sc.runner());
    int changes = 0;
    QObject::connect(&m, &TrayModel::changed, [&] { ++changes; });
    m.refresh();
    EXPECT_EQ(m.serviceLine(), "Background service switched on, but not running");
    m.refresh();
    EXPECT_EQ(changes, 1);                                   // unchanged state: no notification
    sc.load = "not-found";
    m.refresh();
    EXPECT_EQ(m.serviceLine(), "Background service not installed");
    sc.rc = 1;
    m.refresh();
    EXPECT_EQ(m.serviceLine(), "Background service state unknown");
    EXPECT_EQ(changes, 3);
    for (const auto& c : sc.calls) EXPECT_EQ(c.mid(0, 3), (QStringList{"systemctl", "--user", "show"})) << c.join(' ').toStdString();
}

// ---- tray menu ---------------------------------------------------------------------------------------

TEST(TrayController, MenuOffersExactlyOpenSettingsAndQuit) {
    app();
    writeSync(true);
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    sc.active = "active";
    TrayModel m(sc.runner());
    m.refresh();
    QStringList opened;
    TrayController t(m, [&](const QString& page) { opened << page; });
    QStringList enabledActions;
    for (QAction* a : t.menu()->actions())
        if (!a->isSeparator() && a->isEnabled()) enabledActions << a->text();
    EXPECT_EQ(enabledActions, (QStringList{"Open OpenWitty", "Desktop integration settings…", "Quit tray icon"}));
    EXPECT_FALSE(t.knobStatus()->isEnabled());
    EXPECT_FALSE(t.serviceStatus()->isEnabled());
    EXPECT_EQ(t.knobStatus()->text(), "Desktop volume follows the CONNECT 2 knob");
    EXPECT_EQ(t.serviceStatus()->text(), "Keeps working when the window is closed");
    EXPECT_EQ(t.icon()->toolTip(), m.toolTip());
    EXPECT_TRUE(t.quitAction()->toolTip().contains("background service keeps running"));

    t.openAction()->trigger();
    t.openAction()->trigger();                              // repeated: each is a request; the GUI dedups
    t.settingsAction()->trigger();
    emit t.icon()->activated(QSystemTrayIcon::Trigger);     // left click
    emit t.icon()->activated(QSystemTrayIcon::Context);     // right click only opens the menu
    EXPECT_EQ(opened, (QStringList{"", "", "settings-desktop", ""}));
}

TEST(TrayController, QuitLeavesTheBackgroundServiceAlone) {
    app();
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    sc.active = "active";
    TrayModel m(sc.runner());
    m.refresh();
    TrayController t(m, [](const QString&) {});
    int quits = 0;
    QObject::connect(&t, &TrayController::quitRequested, [&] { ++quits; });
    t.quitAction()->trigger();
    EXPECT_EQ(quits, 1);
    EXPECT_FALSE(sc.changedService());
}

TEST(TrayController, StatusFollowsTheSettingsWhenRefreshed) {
    app();
    writeSync(true);
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    sc.active = "active";
    TrayModel m(sc.runner());
    m.refresh();
    TrayController t(m, [](const QString&) {});
    writeSync(false);                                       // the user turned the knob display off in the window
    emit t.menu()->aboutToShow();                           // opening the menu re-reads
    EXPECT_EQ(t.knobStatus()->text(), "Desktop volume does not follow the CONNECT 2 knob");
    EXPECT_TRUE(t.serviceStatus()->text().contains("nothing to do"));
}

// ---- single instance and GUI activation --------------------------------------------------------------

TEST(InstanceChannel, SecondInstanceDeliversItsRequestToTheFirst) {
    app();
    InstanceChannel first("t-deliver");
    ASSERT_EQ(first.acquire(), InstanceChannel::Acquire::Owner);
    InstanceChannel second("t-deliver");
    EXPECT_EQ(second.acquire(), InstanceChannel::Acquire::Busy);   // a live owner is never replaced
    QStringList got;
    QObject::connect(&first, &InstanceChannel::messageReceived, [&](const QString& m) { got << m; });
    EXPECT_TRUE(inThreadWhileProcessingEvents([] { return InstanceChannel::send("t-deliver", "a\nb"); }));
    QCoreApplication::processEvents();
    EXPECT_EQ(got, QStringList{"a\nb"});
    EXPECT_FALSE(InstanceChannel::send("t-nobody", "x", 200));
}

TEST(InstanceChannel, StaleSocketOfACrashedInstanceIsReplaced) {
    app();
    const QString path = InstanceChannel::socketPath("t-stale");
    EXPECT_TRUE(path.startsWith(qEnvironmentVariable("XDG_RUNTIME_DIR")));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    InstanceChannel c("t-stale");
    EXPECT_EQ(c.acquire(), InstanceChannel::Acquire::Owner);
}

TEST(GuiActivation, RunningWindowIsActivatedInsteadOfStartingAnother) {
    app();
    InstanceChannel gui("gui");
    ASSERT_EQ(gui.acquire(), InstanceChannel::Acquire::Owner);
    QStringList got;
    QObject::connect(&gui, &InstanceChannel::messageReceived, [&](const QString& m) { got << m; });
    int spawned = 0;
    auto spawn = [&](const QString&, const QStringList&) { ++spawned; return true; };
    for (int i = 0; i < 3; ++i) {
        const auto r = inThreadWhileProcessingEvents([&] { return openOpenWitty("settings-desktop", "openwitty", spawn); });
        EXPECT_EQ(r, OpenResult::Activated);
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(spawned, 0);
    ASSERT_EQ(got.size(), 3);
    GuiActivation a;
    ASSERT_TRUE(decodeGuiActivation(got.first(), &a));
    EXPECT_EQ(a.page, "settings-desktop");
}

TEST(GuiActivation, WithoutAWindowOpenWittyIsStartedWithThePage) {
    app();
    QList<QStringList> spawned;
    auto spawn = [&](const QString& p, const QStringList& args) { spawned.append(QStringList{p} + args); return true; };
    EXPECT_EQ(openOpenWitty("", "/x/openwitty", spawn), OpenResult::Started);
    EXPECT_EQ(openOpenWitty("settings-desktop", "/x/openwitty", spawn), OpenResult::Started);
    EXPECT_EQ(spawned, (QList<QStringList>{{"/x/openwitty"}, {"/x/openwitty", "--show", "settings-desktop"}}));
    auto failing = [](const QString&, const QStringList&) { return false; };
    EXPECT_EQ(openOpenWitty("", "/x/openwitty", failing), OpenResult::Failed);
    EXPECT_FALSE(decodeGuiActivation("quit", nullptr));
}

// ---- tray autostart ----------------------------------------------------------------------------------

TEST(TrayAutostart, EntryIsWrittenAndRemovedOnlyOnRequest) {
    app();
    disableTrayAutostart();
    EXPECT_FALSE(trayAutostartEnabled());
    const QString dir = QFileInfo(trayAutostartPath()).absolutePath();
    ASSERT_TRUE(QDir().mkpath(dir));
    QFile other(dir + "/someone-else.desktop");
    ASSERT_TRUE(other.open(QIODevice::WriteOnly));
    other.close();
    ASSERT_TRUE(enableTrayAutostart("/opt/Open Witty/bin/openwitty-tray"));
    EXPECT_TRUE(trayAutostartEnabled());
    QFile f(trayAutostartPath());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    EXPECT_TRUE(text.contains("Exec=\"/opt/Open Witty/bin/openwitty-tray\"\n")) << text.toStdString();
    EXPECT_TRUE(text.contains("Type=Application\n"));
    EXPECT_TRUE(text.contains("NoDisplay=true\n"));
    EXPECT_TRUE(disableTrayAutostart());
    EXPECT_FALSE(trayAutostartEnabled());
    EXPECT_TRUE(disableTrayAutostart());                    // idempotent
    EXPECT_TRUE(QFileInfo::exists(dir + "/someone-else.desktop"));
    EXPECT_EQ(trayAutostartContents("/usr/bin/openwitty-tray").count("Exec=/usr/bin/openwitty-tray\n"), 1);
}

TEST(IntegrationSettingsTray, SwitchWritesTheLoginEntryStartsAndQuitsTheIconButNeverTheService) {
    app();
    disableTrayAutostart();
    FakeSystemctl sc;
    sc.unitFile = "enabled";
    sc.active = "active";
    QList<QStringList> launched;
    auto launcher = [&](const QString& p, const QStringList& a) { launched.append(QStringList{p} + a); return true; };
    const QString tray = QCoreApplication::applicationFilePath();   // an existing executable stands in
    IntegrationSettings s(nullptr, sc.runner(), [] { return std::optional<lewitt::AudioSetupStatus>{}; }, launcher, tray);
    EXPECT_TRUE(s.trayInstalled());
    EXPECT_FALSE(s.trayAutostart());
    EXPECT_TRUE(s.setTrayAutostart(true));
    EXPECT_TRUE(s.trayAutostart());
    EXPECT_TRUE(trayAutostartEnabled());
    EXPECT_EQ(launched, QList<QStringList>{{tray}});
    EXPECT_TRUE(s.setTrayAutostart(false));
    EXPECT_FALSE(trayAutostartEnabled());
    EXPECT_TRUE(sc.calls.contains(QStringList{tray, "--quit"}));
    EXPECT_FALSE(sc.changedService());
}

TEST(IntegrationSettingsTray, ResetAlsoRemovesTheTrayLoginEntry) {
    app();
    FakeSystemctl sc;   // service off
    auto launcher = [](const QString&, const QStringList&) { return true; };
    IntegrationSettings s(nullptr, sc.runner(), [] { return std::optional<lewitt::AudioSetupStatus>{}; }, launcher,
                          QCoreApplication::applicationFilePath());
    ASSERT_TRUE(s.setTrayAutostart(true));
    EXPECT_TRUE(s.resetToDefaults());
    EXPECT_FALSE(trayAutostartEnabled());
    EXPECT_FALSE(s.trayAutostart());
}

TEST(IntegrationSettingsTray, MissingTrayProgramIsReportedAndNothingIsWritten) {
    app();
    disableTrayAutostart();
    FakeSystemctl sc;
    int launched = 0;
    auto launcher = [&](const QString&, const QStringList&) { ++launched; return true; };
    IntegrationSettings s(nullptr, sc.runner(), [] { return std::optional<lewitt::AudioSetupStatus>{}; }, launcher,
                          "/nonexistent/openwitty-tray");
    EXPECT_FALSE(s.trayInstalled());
    EXPECT_FALSE(s.setTrayAutostart(true));
    EXPECT_FALSE(trayAutostartEnabled());
    EXPECT_EQ(launched, 0);
    EXPECT_TRUE(s.lastError().contains("not installed"));
}

// Found by a real makepkg run: a runtime directory so deep that the socket path exceeds the Unix limit.
// The lock still makes this process the one owner (it must start); others give up instead of duplicating.
TEST(InstanceChannel, OwnerWithoutASocketStillOwnsAndOthersDoNotDuplicate) {
    app();
    QTemporaryDir base;
    QString deep = base.path();
    while (deep.size() < 140) deep += "/very-long-directory-name";
    ASSERT_TRUE(QDir().mkpath(deep));
    const QByteArray prev = qgetenv("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", deep.toUtf8());
    {
        InstanceChannel first("t-deep");
        EXPECT_EQ(first.acquire(), InstanceChannel::Acquire::Owner);
        EXPECT_TRUE(first.owner());
        EXPECT_FALSE(first.listening());
        EXPECT_TRUE(first.lastError().contains("cannot listen")) << first.lastError().toStdString();
        InstanceChannel second("t-deep");
        EXPECT_EQ(second.claimOrForward("activate\n", 400), InstanceChannel::Role::Failed);
        EXPECT_FALSE(second.owner());
    }
    qputenv("XDG_RUNTIME_DIR", prev);
}

TEST(TrayModel, ShowsWhetherTheConnect2IsPluggedIn) {
    app();
    FakeSystemctl sc;
    TrayModel m(sc.runner());
    bool plugged = true;
    m.setPresenceProbe([&] { return plugged; });
    m.refresh();
    EXPECT_EQ(m.deviceLine(), "CONNECT 2 connected");
    EXPECT_TRUE(m.toolTip().startsWith("OpenWitty\nCONNECT 2 connected\n"));
    int changes = 0;
    QObject::connect(&m, &TrayModel::changed, [&] { ++changes; });
    plugged = false;
    m.refresh();
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(m.deviceLine(), "CONNECT 2 not connected");
}

// ---- window at login, reopen where it was closed ----------------------------------------------------

TEST(WindowAutostart, EntryIsWrittenAndRemovedOnlyOnRequestNextToTheTrayEntry) {
    app();
    disableWindowAutostart();
    disableTrayAutostart();
    EXPECT_FALSE(windowAutostartEnabled());
    EXPECT_NE(windowAutostartPath(), trayAutostartPath());
    ASSERT_TRUE(enableWindowAutostart("/opt/Open Witty/bin/openwitty"));
    EXPECT_TRUE(windowAutostartEnabled());
    EXPECT_FALSE(trayAutostartEnabled());                   // independent of the tray entry
    QFile f(windowAutostartPath());
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    EXPECT_TRUE(text.contains("Exec=\"/opt/Open Witty/bin/openwitty\"\n")) << text.toStdString();
    EXPECT_TRUE(text.contains("Type=Application\n"));
    EXPECT_TRUE(disableWindowAutostart());
    EXPECT_FALSE(windowAutostartEnabled());
    EXPECT_TRUE(disableWindowAutostart());                  // idempotent
}

TEST(WindowAutostart, SwitchWritesAndRemovesTheEntryAndResetRemovesIt) {
    app();
    disableWindowAutostart();
    FakeSystemctl sc;
    IntegrationSettings s(nullptr, sc.runner(), [] { return std::optional<lewitt::AudioSetupStatus>{}; },
                          [](const QString&, const QStringList&) { return true; }, QCoreApplication::applicationFilePath());
    EXPECT_FALSE(s.windowAutostart());
    EXPECT_TRUE(s.setWindowAutostart(true));
    EXPECT_TRUE(s.windowAutostart());
    EXPECT_TRUE(windowAutostartEnabled());
    EXPECT_TRUE(s.setWindowAutostart(false));
    EXPECT_FALSE(windowAutostartEnabled());
    ASSERT_TRUE(s.setWindowAutostart(true));
    EXPECT_TRUE(s.resetToDefaults());
    EXPECT_FALSE(windowAutostartEnabled());
    EXPECT_FALSE(sc.changedService());
}

TEST(WindowPlacement, ModeDependsOnTheDesktopAndSession) {
    using windowplace::detectMode;
    EXPECT_EQ(detectMode("KDE", "wayland", true), "kwin");
    EXPECT_EQ(detectMode("ubuntu:KDE", "xcb", true), "kwin");
    EXPECT_EQ(detectMode("KDE", "wayland", false), "unsupported");   // no kwriteconfig6
    EXPECT_EQ(detectMode("GNOME", "xcb", false), "app");
    EXPECT_EQ(detectMode("GNOME", "wayland", false), "unsupported");
    EXPECT_EQ(detectMode("", "offscreen", true), "unsupported");
}

TEST(WindowPlacement, GeometryIsParsedOnlyWhenSane) {
    int x = 0, y = 0, w = 0, h = 0;
    EXPECT_TRUE(windowplace::parseGeometry("100,-20,1200,800", &x, &y, &w, &h));
    EXPECT_EQ(QList<int>({x, y, w, h}), QList<int>({100, -20, 1200, 800}));
    for (const char* bad : {"", "1,2,3", "a,b,c,d", "0,0,10,10", "0,0,99999,800", "1,2,3,4,5"})
        EXPECT_FALSE(windowplace::parseGeometry(bad, &x, &y, &w, &h)) << bad;
}

namespace {
// kreadconfig6 / kwriteconfig6 over an in-memory kwinrulesrc ("group/key" -> value)
struct FakeKConfig {
    QMap<QString, QString> v;
    QList<QStringList> calls;
    CommandRunner runner() {
        return [this](const QString& program, const QStringList& a, QString* out) {
            calls.append(QStringList{program} + a);
            if (program != "kreadconfig6" && program != "kwriteconfig6") return 0;
            if (a.value(0) != "--file" || a.value(1) != "kwinrulesrc") return 1;
            const QString k = a.value(3) + "/" + a.value(5);
            if (program == "kreadconfig6") { if (out) *out = v.value(k) + "\n"; return 0; }
            if (a.value(6) == "--delete") v.remove(k); else v[k] = a.value(6);
            return 0;
        };
    }
};
}  // namespace

TEST(WindowPlacement, KdeRuleIsWrittenRemovedAndNeverTouchesOtherRules) {
    app();
    FakeKConfig kc;
    kc.v = {{"1/Description", "someone else's rule"}, {"1/minimizerule", "3"}, {"General/Order", "1"}};
    const auto other = kc.v;
    IntegrationSettings s(nullptr, kc.runner(), [] { return std::optional<lewitt::AudioSetupStatus>{}; },
                          [](const QString&, const QStringList&) { return true; }, QCoreApplication::applicationFilePath());
    EXPECT_NE(s.placementMode(), "kwin");                   // the test environment has no KDE session
    s.setPlacementModeForTesting("unsupported");
    EXPECT_FALSE(s.setKwinPositionRule(true));               // nothing written outside KDE
    for (const auto& c : kc.calls) EXPECT_NE(c.value(0), "kwriteconfig6");
    s.setPlacementModeForTesting("kwin");
    EXPECT_FALSE(s.kwinPositionRule());
    ASSERT_TRUE(s.setKwinPositionRule(true));
    EXPECT_TRUE(s.kwinPositionRule());
    EXPECT_EQ(kc.v.value("openwitty-window/wmclass"), "io.github.st0nebyte.openwitty");
    EXPECT_EQ(kc.v.value("openwitty-window/wmclassmatch"), "1");
    EXPECT_EQ(kc.v.value("openwitty-window/positionrule"), "4");
    EXPECT_EQ(kc.v.value("openwitty-window/sizerule"), "4");
    EXPECT_EQ(kc.v.value("General/Order"), "1,openwitty-window");
    EXPECT_FALSE(kc.v.contains("General/rules"));            // no legacy list created
    EXPECT_TRUE(kc.calls.contains(windowplace::reconfigureCommand()));
    for (const auto& c : kc.calls)
        if (c.value(0) == "kwriteconfig6") EXPECT_TRUE(c.value(4) == "openwitty-window" || c.value(4) == "General") << c.join(' ').toStdString();
    ASSERT_TRUE(s.setKwinPositionRule(true));                // again: Order not duplicated
    EXPECT_EQ(kc.v.value("General/Order"), "1,openwitty-window");
    kc.v["openwitty-window/position"] = "100,80";            // KWin remembered a place
    ASSERT_TRUE(s.setKwinPositionRule(false));
    EXPECT_FALSE(s.kwinPositionRule());
    for (auto it = kc.v.cbegin(); it != kc.v.cend(); ++it) EXPECT_FALSE(it.key().startsWith("openwitty-window/")) << it.key().toStdString();
    for (auto it = other.cbegin(); it != other.cend(); ++it)
        if (it.key() != "General/Order") EXPECT_EQ(kc.v.value(it.key()), it.value());
    ASSERT_TRUE(s.setKwinPositionRule(true));
    EXPECT_TRUE(s.resetToDefaults());                        // reset removes the rule too
    EXPECT_FALSE(kc.v.contains("openwitty-window/positionrule"));
}

TEST(WindowPlacement, LegacyRuleListGetsTheGroupSoKwinKeepsIt) {
    const auto c = windowplace::ruleOnCommands("app", "", "a, b");
    EXPECT_TRUE(c.contains(QStringList{"kwriteconfig6", "--file", "kwinrulesrc", "--group", "General", "--key", "rules",
                                       "a,b,openwitty-window"}));
    for (const auto& x : windowplace::ruleOnCommands("app", "", ""))
        EXPECT_NE(x.value(4), "General");                    // no list: KWin lists the group by itself
}
