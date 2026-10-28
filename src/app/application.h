#pragma once

#include <QApplication>

#include <memory>

class QSettings;

namespace mtb {

class MainWindow;
class Settings;
class ToolHistory;
class ToolRegistry;

class Application final : public QApplication {
public:
    Application(int& argc, char** argv);
    ~Application() override;

    int run();

private:
    std::unique_ptr<QSettings> storage_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<ToolHistory> history_;
    std::unique_ptr<ToolRegistry> registry_;
    std::unique_ptr<MainWindow> mainWindow_;
};

}  // namespace mtb
