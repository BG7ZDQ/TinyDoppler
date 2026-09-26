#include "satellite_tracker_dialog.h"
#include "catalog_dialog.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <QTimer>

namespace {
void installTranslation(QApplication& application, QTranslator& translator)
{
    const QStringList arguments = application.arguments();
    const bool chinese = arguments.contains(QStringLiteral("--language=zh")) ||
        (!arguments.contains(QStringLiteral("--language=en")) &&
         !arguments.contains(QStringLiteral("--language=ja")) &&
         QLocale::system().language() == QLocale::Chinese);
    if (chinese)
        return;
    const bool japanese = arguments.contains(QStringLiteral("--language=ja")) ||
        (!arguments.contains(QStringLiteral("--language=en")) &&
         QLocale::system().language() == QLocale::Japanese);
    if (translator.load(japanese ? QStringLiteral(":/tiny/tiny_ja.qm")
                                 : QStringLiteral(":/tiny/tiny_en.qm")))
        application.installTranslator(&translator);
}
}

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication application(argc, argv);
    QTranslator translator;
    installTranslation(application, translator);
    application.setWindowIcon(QIcon(QStringLiteral(":/tiny/icon.png")));
    QFont font = application.font();
    font.setPointSize(10);
    application.setFont(font);
    QCoreApplication::setOrganizationName(QStringLiteral("BG7ZDQ"));
    QCoreApplication::setApplicationName(QStringLiteral("TinyDoppler"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption longitude(QStringLiteral("longitude"),
                                       QStringLiteral("Observer longitude"),
                                       QStringLiteral("degrees"), QStringLiteral("0"));
    const QCommandLineOption latitude(QStringLiteral("latitude"),
                                      QStringLiteral("Observer latitude"),
                                      QStringLiteral("degrees"), QStringLiteral("0"));
    const QCommandLineOption altitude(QStringLiteral("altitude"),
                                      QStringLiteral("Observer altitude"),
                                      QStringLiteral("metres"), QStringLiteral("0"));
    const QCommandLineOption satellite(QStringLiteral("satellite"),
                                       QStringLiteral("Preferred satellite"),
                                       QStringLiteral("name"), QStringLiteral("ASRTU-1"));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a window screenshot"),
                                        QStringLiteral("path"));
    const QCommandLineOption screenshotCatalog(
        QStringLiteral("screenshot-catalog"),
        QStringLiteral("Save a satellite catalog screenshot"),
        QStringLiteral("path"));
    const QCommandLineOption language(QStringLiteral("language"),
                                      QStringLiteral("UI language"),
                                      QStringLiteral("code"));
    parser.addOptions({longitude, latitude, altitude, satellite, screenshot,
                       screenshotCatalog, language});
    parser.process(application);

    QSettings settings(QStringLiteral("TinyDoppler"), QStringLiteral("Tracker"));
    const double longitudeDeg = parser.isSet(longitude)
                                    ? parser.value(longitude).toDouble()
                                    : settings.value(QStringLiteral("longitude"), 0.0).toDouble();
    const double latitudeDeg = parser.isSet(latitude)
                                   ? parser.value(latitude).toDouble()
                                   : settings.value(QStringLiteral("latitude"), 0.0).toDouble();
    const double altitudeMeters = parser.isSet(altitude)
                                      ? parser.value(altitude).toDouble()
                                      : settings.value(QStringLiteral("altitude"), 0.0).toDouble();

    if (parser.isSet(screenshotCatalog)) {
        SatelliteCatalog catalog;
        QString error;
        if (!catalog.load(&error))
            return 2;
        CatalogDialog catalogWindow(catalog);
        catalogWindow.show();
        const QString path = parser.value(screenshotCatalog);
        QTimer::singleShot(500, &catalogWindow,
                           [&application, &catalogWindow, path] {
                               catalogWindow.grab().save(path);
                               application.quit();
                           });
        return application.exec();
    }

    SatelliteTrackerDialog window(longitudeDeg, latitudeDeg, altitudeMeters,
                                  parser.value(satellite));
    window.show();
    if (parser.isSet(screenshot)) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(1200, &window, [&application, &window, path] {
            window.grab().save(path);
            application.quit();
        });
    }
    return application.exec();
}
