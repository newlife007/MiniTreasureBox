#include "app/application.h"

#include "app/main_window.h"
#include "ai/handwriting_removal/restoration_engine.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"

#include <QSettings>

namespace mtb {

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("MiniTreasureBox"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("minitreasurebox.local"));
    QCoreApplication::setApplicationName(QStringLiteral("MiniTreasureBox"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MINITREASUREBOX_VERSION));
    handwriting::RestorationWorkerEngine::cleanupStaleTasks();
    setQuitOnLastWindowClosed(true);
}

Application::~Application() = default;

int Application::run()
{
    storage_ = std::make_unique<QSettings>();
    settings_ = std::make_unique<Settings>(*storage_);
    history_ = std::make_unique<ToolHistory>(*storage_);
    registry_ = std::make_unique<ToolRegistry>();
    registerBuiltinTools(*registry_);
    mainWindow_ = std::make_unique<MainWindow>(*registry_, *settings_, *history_);
    mainWindow_->show();
    mainWindow_->startLocalFileIndexing();
    return exec();
}

}  // namespace mtb
