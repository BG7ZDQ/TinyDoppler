#include "satellite_tracker_dialog.h"
#include "catalog_dialog.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <QTimer>

namespace {
QString uiLanguage(const QApplication& application)
{
    const QStringList arguments = application.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        QString value;
        if (arguments.at(i).startsWith(QStringLiteral("--language=")))
            value = arguments.at(i).mid(11);
        else if (arguments.at(i) == QStringLiteral("--language") && i + 1 < arguments.size())
            value = arguments.at(++i);
        if (value == QStringLiteral("zh") || value == QStringLiteral("en") ||
            value == QStringLiteral("ja"))
            return value;
    }
    QSettings installed(QDir(QCoreApplication::applicationDirPath()).filePath(
                            QStringLiteral("ui-language.ini")), QSettings::IniFormat);
    const QString saved = installed.value(QStringLiteral("UI/Language")).toString();
    if (saved == QStringLiteral("zh") || saved == QStringLiteral("en") ||
        saved == QStringLiteral("ja"))
        return saved;
    if (QLocale::system().language() == QLocale::Chinese)
        return QStringLiteral("zh");
    if (QLocale::system().language() == QLocale::Japanese)
        return QStringLiteral("ja");
    return QStringLiteral("en");
}

void installTranslation(QApplication& application, QTranslator& translator)
{
    const QString language = uiLanguage(application);
    const bool chinese = language == QStringLiteral("zh");
    if (chinese)
        return;
    const bool japanese = language == QStringLiteral("ja");
    if (translator.load(japanese ? QStringLiteral(":/tiny/tiny_ja.qm")
                                 : QStringLiteral(":/tiny/tiny_en.qm")))
        application.installTranslator(&translator);
}

void configureUiFont(QApplication& application)
{
    const QString language = uiLanguage(application);
    const bool chinese = language == QStringLiteral("zh");
    const bool japanese = language == QStringLiteral("ja");
#ifdef Q_OS_WIN
    const QStringList preferred = chinese
        ? QStringList{QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"), QStringLiteral("Segoe UI")}
        : japanese
            ? QStringList{QStringLiteral("Yu Gothic UI"), QStringLiteral("Meiryo"), QStringLiteral("Segoe UI")}
            : QStringList{QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI")};
#else
    const QStringList preferred = chinese
        ? QStringList{QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")}
        : japanese
            ? QStringList{QStringLiteral("Noto Sans CJK JP"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")}
            : QStringList{QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")};
#endif
    QFont font = application.font();
    const QFontDatabase database;
    for (const QString& family : preferred) {
        if (database.hasFamily(family)) {
            font.setFamily(family);
            break;
        }
    }
    font.setPointSizeF(10.0);
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(
        QFont::PreferAntialias | QFont::PreferQuality));
    application.setFont(font);
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
    configureUiFont(application);
    QCoreApplication::setOrganizationName(QStringLiteral("BG7ZDQ"));
    QCoreApplication::setApplicationName(QStringLiteral("TinyDoppler"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption integrated(QStringLiteral("integrated"),
        QStringLiteral("Use the receiver launcher ground station settings"));
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
                                       QStringLiteral("name"));
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
                       screenshotCatalog, language, integrated});
    parser.process(application);

    bool integratedMode = parser.isSet(integrated);
#ifdef TINY_DOPPLER_RECEIVER_BUILD
    integratedMode = true;
#endif
    // Keep the receiver's existing profile; standalone use has its own
    // catalog, settings and orbit cache, without inheriting receiver presets.
    if (!integratedMode)
        QCoreApplication::setApplicationName(QStringLiteral("TinyDopplerStandalone"));
    QSettings settings(QStringLiteral("TinyDoppler"),
                       integratedMode ? QStringLiteral("Tracker")
                                      : QStringLiteral("StandaloneTracker"));
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
                                  parser.value(satellite), nullptr,
                                  integratedMode);
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
