#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() < 5 || arguments.at(2) != QStringLiteral("stdout")
        || arguments.at(3) != QStringLiteral("-l")) {
        QTextStream(stderr) << "invalid arguments";
        return 4;
    }
    const QString imageName = QFileInfo(arguments.at(1)).baseName();
    const QString language = arguments.at(4);
    QString behavior = qEnvironmentVariable("MINITREASUREBOX_FAKE_TESSERACT_MODE");
    if (behavior.isEmpty()) {
        behavior = language;
    }
    const QString capturePath = qEnvironmentVariable("MINITREASUREBOX_FAKE_TESSERACT_ARGUMENTS");
    if (!capturePath.isEmpty()) {
        QFile capture(capturePath);
        if (capture.open(QIODevice::WriteOnly | QIODevice::Text)) {
            capture.write(arguments.mid(1).join(QLatin1Char('\n')).toUtf8());
        }
    }
    if (behavior == QStringLiteral("missing")) {
        QTextStream(stderr) << "Error opening data file";
        return 1;
    }
    if (behavior == QStringLiteral("fail")) {
        QTextStream(stderr) << "fixture failure";
        return 2;
    }
    if (behavior == QStringLiteral("slow")) {
        QThread::msleep(1000);
        QTextStream(stdout) << "too late";
        return 0;
    }
    if (behavior == QStringLiteral("empty")) {
        return 0;
    }
    QTextStream(stdout) << "fixture text for " << imageName;
    return 0;
}
