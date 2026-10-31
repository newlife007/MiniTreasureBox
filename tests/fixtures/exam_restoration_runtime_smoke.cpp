#include "ai/handwriting_removal/restoration_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        QTextStream(stderr)
            << "usage: exam_restoration_runtime_smoke <installed-application-directory>\n";
        return 2;
    }

    const QString applicationDirectory =
        QDir::cleanPath(application.arguments().at(1));
    const auto runtime = mtb::handwriting::RestorationWorkerEngine::resolvedConfig(
        {}, applicationDirectory);
    mtb::handwriting::RestorationWorkerEngine engine(runtime);

    QImage input(32, 32, QImage::Format_ARGB32);
    input.fill(qRgb(248, 248, 246));
    const auto result = engine.detectAndRemove(input);
    if (!result.ok) {
        QTextStream(stderr) << "packaged exam-restoration processing failed: "
                            << result.message << '\n';
        return 3;
    }
    if (result.metadata.executionMode
        != mtb::handwriting::ExecutionMode::OfflineModel) {
        QTextStream(stderr)
            << "production resolver used baseline mode instead of the packaged runtime\n";
        return 4;
    }
    if (result.restored.size() != input.size() || result.mask.size() != input.size()) {
        QTextStream(stderr) << "packaged exam-restoration output dimensions differ\n";
        return 5;
    }

    QTextStream(stdout)
        << "Bundled exam-restoration worker processed an image through the production resolver\n";
    return 0;
}
