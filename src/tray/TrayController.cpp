#include "TrayController.hpp"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

#include "TrayModel.hpp"

TrayController::TrayController(TrayModel& model, OpenGui open, QObject* parent)
    : QObject(parent), model_(model), openGui_(std::move(open)), menu_(std::make_unique<QMenu>()) {
    knob_ = menu_->addAction(QString());
    knob_->setEnabled(false);
    service_ = menu_->addAction(QString());
    service_->setEnabled(false);
    menu_->addSeparator();
    open_ = menu_->addAction(QStringLiteral("Open OpenWitty"));
    settings_ = menu_->addAction(QStringLiteral("Desktop integration settings…"));
    menu_->addSeparator();
    quit_ = menu_->addAction(QStringLiteral("Quit tray icon"));
    quit_->setToolTip(QStringLiteral("Removes this icon. The background service keeps running."));
    menu_->setToolTipsVisible(true);

    connect(open_, &QAction::triggered, this, [this] { openGui_(QString()); });
    connect(settings_, &QAction::triggered, this, [this] { openGui_(QStringLiteral("settings-desktop")); });
    connect(quit_, &QAction::triggered, this, &TrayController::quitRequested);
    connect(menu_.get(), &QMenu::aboutToShow, &model_, &TrayModel::refresh);
    connect(&model_, &TrayModel::changed, this, &TrayController::update);

    icon_ = new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("openwitty"), QIcon(QStringLiteral(":/openwitty.png"))), this);
    icon_->setContextMenu(menu_.get());
    connect(icon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) openGui_(QString());   // left click opens the window
    });
    update();
}

TrayController::~TrayController() = default;

void TrayController::show() { icon_->show(); }

void TrayController::update() {
    knob_->setText(model_.knobLine());
    service_->setText(model_.serviceLine());
    icon_->setToolTip(model_.toolTip());
}
