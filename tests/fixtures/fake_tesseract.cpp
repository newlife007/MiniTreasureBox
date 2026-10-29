#include <QCoreApplication>
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
    if (language == QStringLiteral("missing")) {
        QTextStream(stderr) << "Error opening data file";
        return 1;
    }
    if (language == QStringLiteral("fail")) {
        QTextStream(stderr) << "fixture failure";
        return 2;
    }
    if (language == QStringLiteral("slow")) {
        QThread::msleep(1000);
        QTextStream(stdout) << "too late";
        return 0;
    }
    if (language == QStringLiteral("empty")) {
        return 0;
    }
    QTextStream(stdout) << "fixture text for " << imageName;
    return 0;
}
