#include "satellite_catalog.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("TinyDopplerTest"));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir configDirectory;
    assert(configDirectory.isValid());
    qputenv("TINY_DOPPLER_CONFIG_DIR", configDirectory.path().toLocal8Bit());

    SatelliteCatalog fresh;
    QString freshError;
    const bool freshLoaded = fresh.load(&freshError);
    assert(freshLoaded);
#ifdef TINY_DOPPLER_RECEIVER_BUILD
    assert(fresh.entries().size() == 3);
    assert(fresh.find(61781) && fresh.find(100470) && fresh.find(100469));
    assert(fresh.find(61781)->name == QStringLiteral("ASRTU-1 (AO-123)"));
    assert(fresh.find(100469)->name == QStringLiteral("BY70-4 (Harbin MicroSat)"));
    assert(fresh.find(100470)->name == QStringLiteral("JAMX01 (JING'AN DREAM STAR)"));
    assert(fresh.find(100470)->frequenciesHz.contains(435175000));
    assert(fresh.find(61781)->frequencyNotes.value(435400000) == QStringLiteral("TLM"));
    assert(fresh.find(61781)->frequencyNotes.value(436210000) == QStringLiteral("Image"));
    assert(fresh.find(100469)->frequenciesHz == QList<qint64>{437433000});
    assert(fresh.find(100469)->selectedHz == 437433000);
    assert(fresh.find(100469)->frequencyNotes.value(437433000) == QStringLiteral("TLM / Image"));
    assert(SatelliteCatalog::defaultSources().size() == 3);
    for (const auto& source : SatelliteCatalog::defaultSources()) {
        assert(source.startsWith(QStringLiteral("https://celestrak.org/")));
        assert(source.endsWith(QStringLiteral("FORMAT=JSON")));
    }
#else
    assert(fresh.entries().isEmpty());
    assert(SatelliteCatalog::defaultSources().isEmpty());
    assert(!QFile::exists(QStringLiteral(":/tiny/default_satellites.json")));
#endif

    QFile oldCatalog(SatelliteCatalog::filePath());
    const bool opened = oldCatalog.open(QIODevice::WriteOnly);
    assert(opened);
    const QByteArray oldConfiguration = R"({
      "schemaVersion":1, "catalogRevision":2,
      "satellites":[{"norad":98247,"name":"BY04",
        "frequenciesHz":[437443000,437500000],"selectedHz":437500000}]
    })";
    const qint64 written = oldCatalog.write(oldConfiguration);
    assert(written == oldConfiguration.size());
    oldCatalog.close();

    int number = 0;
    const bool alphaParsed = SatelliteCatalog::parseNorad(QStringLiteral("A0123"), &number);
    assert(alphaParsed);
    assert(number == 100123);
    const bool decimalParsed = SatelliteCatalog::parseNorad(QStringLiteral("100465"), &number);
    assert(decimalParsed);
    assert(number == 100465);
    const bool lowerParsed = SatelliteCatalog::parseNorad(QStringLiteral("a0465"), &number);
    assert(lowerParsed);
    assert(number == 100465);
    const bool invalidLengthParsed = SatelliteCatalog::parseNorad(QStringLiteral("A01234"), &number);
    const bool invalidLetterParsed = SatelliteCatalog::parseNorad(QStringLiteral("I0000"), &number);
    assert(!invalidLengthParsed);
    assert(!invalidLetterParsed);
    assert(SatelliteCatalog::alpha5(100465) == QStringLiteral("A0465"));
    assert(SatelliteCatalog::alpha5(339999) == QStringLiteral("Z9999"));

    SatelliteCatalog catalog;
    QString error;
    if (!catalog.load(&error)) {
        std::fprintf(stderr, "Catalog load failed: %s (%s)\n",
                     error.toUtf8().constData(),
                     SatelliteCatalog::filePath().toUtf8().constData());
        return 1;
    }
#ifdef TINY_DOPPLER_RECEIVER_BUILD
    const SatelliteProfile* asrtu = catalog.find(61781);
    assert(asrtu && asrtu->selectedHz == 435400000);
    const SatelliteProfile* jamx = catalog.find(100470);
    assert(jamx && jamx->frequenciesHz.contains(435075000));
    const SatelliteProfile* by04 = catalog.find(100469);
    assert(by04 && by04->frequenciesHz.contains(437433000) &&
           by04->selectedHz == 437500000);
    assert(!by04->frequenciesHz.contains(437443000));
    assert(!catalog.find(98247));
    assert(by04->name == QStringLiteral("BY70-4 (Harbin MicroSat)"));
    assert(oldCatalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
    oldCatalog.write(R"({"schemaVersion":1,"catalogRevision":3,"satellites":[
        {"norad":100469,"name":"BY04","frequenciesHz":[437500000],
         "selectedHz":437500000,"frequencyNotes":{"437500000":"Telemetry"}},
        {"norad":100465,"name":"My custom name","frequenciesHz":[],"selectedHz":0}
    ]})");
    oldCatalog.close();
    SatelliteCatalog upgraded;
    assert(upgraded.load(&error));
    assert(upgraded.entries().size() == 2 && !upgraded.find(61781));
    assert(upgraded.find(100469)->name == QStringLiteral("BY70-4 (Harbin MicroSat)"));
    assert(upgraded.find(100469)->selectedHz == 437500000);
    assert(upgraded.find(100469)->frequencyNotes.value(437500000) == QStringLiteral("Telemetry"));
    assert(upgraded.find(100465)->name == QStringLiteral("My custom name"));
    assert(oldCatalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
    oldCatalog.write(R"json({"schemaVersion":1,"catalogRevision":4,"satellites":[
        {"norad":100465,"name":"JAMX01 (JING'AN DREAM STAR)",
         "frequenciesHz":[435075000],"selectedHz":435075000,
         "frequencyNotes":{"435075000":"Telemetry"}}
    ]})json");
    oldCatalog.close();
    SatelliteCatalog corrected;
    assert(corrected.load(&error));
    assert(corrected.entries().size() == 1 && !corrected.find(100465));
    assert(corrected.find(100470)->selectedHz == 435075000);
    assert(corrected.find(100470)->frequencyNotes.value(435075000) == QStringLiteral("Telemetry"));
    assert(oldCatalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
    oldCatalog.write(R"({"schemaVersion":1,"catalogRevision":5,"satellites":[
        {"norad":61781,"name":"ASRTU-1","frequenciesHz":[435400000,436210000],
         "selectedHz":436210000,"frequencyNotes":{"435400000":"Custom TLM"}},
        {"norad":100469,"name":"BY70-4","frequenciesHz":[437443000,437433000],
         "selectedHz":437443000,"frequencyNotes":{"437443000":"Custom downlink"}}
    ]})");
    oldCatalog.close();
    SatelliteCatalog annotated;
    assert(annotated.load(&error));
    assert(annotated.entries().size() == 2 && !annotated.find(100470));
    assert(annotated.find(61781)->selectedHz == 436210000);
    assert(annotated.find(61781)->frequencyNotes.value(435400000) == QStringLiteral("Custom TLM"));
    assert(annotated.find(61781)->frequencyNotes.value(436210000) == QStringLiteral("Image"));
    assert(annotated.find(100469)->frequenciesHz == QList<qint64>{437433000});
    assert(annotated.find(100469)->selectedHz == 437433000);
    assert(annotated.find(100469)->frequencyNotes.value(437433000) == QStringLiteral("Custom downlink"));
    assert(!annotated.find(100469)->frequencyNotes.contains(437443000));
    auto annotatedEntries = annotated.entries();
    annotatedEntries[0].frequencyNotes.remove(436210000);
    annotated.setEntries(annotatedEntries);
    assert(annotated.save(&error));
    SatelliteCatalog annotatedReloaded;
    assert(annotatedReloaded.load(&error));
    assert(annotatedReloaded.find(61781)->frequencyNotes.value(436210000).isEmpty());
    assert(annotatedReloaded.find(100469)->selectedHz == 437433000);
    for (const QString& previousName : {QStringLiteral("BY-04"),
                                        QStringLiteral("BY70-4 (8104 Harbin MicroSat)"),
                                        QStringLiteral("My custom BY70-4")}) {
        assert(oldCatalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QJsonObject configuration = QJsonDocument::fromJson(R"json({
            "schemaVersion":1,"catalogRevision":6,"satellites":[
                {"norad":100469,"name":"BY-04","frequenciesHz":[437500000,437433000],
                 "selectedHz":437500000,"frequencyNotes":{"437500000":"Custom note"}}
            ]})json").object();
        auto satellite = configuration.value(QStringLiteral("satellites")).toArray().at(0).toObject();
        satellite.insert(QStringLiteral("name"), previousName);
        configuration.insert(QStringLiteral("satellites"), QJsonArray{satellite});
        const QByteArray bytes = QJsonDocument(configuration).toJson();
        assert(oldCatalog.write(bytes) == bytes.size());
        oldCatalog.close();
        SatelliteCatalog renamed;
        assert(renamed.load(&error));
        assert(renamed.entries().size() == 1);
        const auto* profile = renamed.find(100469);
        assert(profile);
        const QString expectedName = previousName.startsWith(QStringLiteral("My custom"))
            ? previousName : QStringLiteral("BY70-4 (Harbin MicroSat)");
        assert(profile->name == expectedName);
        assert((profile->frequenciesHz == QList<qint64>{437500000, 437433000}));
        assert(profile->selectedHz == 437500000);
        assert(profile->frequencyNotes.value(437500000) == QStringLiteral("Custom note"));
        assert(!profile->frequencyNotes.contains(437433000));
        SatelliteCatalog renamedReloaded;
        assert(renamedReloaded.load(&error));
        assert(renamedReloaded.find(100469)->name == expectedName);
    }
    for (bool alreadyConfigured : {false, true}) {
        QJsonObject configuration = QJsonDocument::fromJson(R"({
            "schemaVersion":1,"catalogRevision":6,"satellites":[
                {"norad":100470,"name":"My JAMX","frequenciesHz":[435075000],
                 "selectedHz":435075000,"frequencyNotes":{"435075000":"Custom note"}}
            ]})").object();
        if (alreadyConfigured) {
            auto satellite = configuration.value(QStringLiteral("satellites")).toArray().at(0).toObject();
            satellite.insert(QStringLiteral("frequenciesHz"), QJsonArray{435075000, 435175000});
            satellite.insert(QStringLiteral("selectedHz"), 435175000);
            satellite.insert(QStringLiteral("frequencyNotes"), QJsonObject{
                {QStringLiteral("435075000"), QStringLiteral("Custom note")},
                {QStringLiteral("435175000"), QStringLiteral("Custom CW")}});
            configuration.insert(QStringLiteral("satellites"), QJsonArray{satellite});
        }
        assert(oldCatalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = QJsonDocument(configuration).toJson();
        assert(oldCatalog.write(bytes) == bytes.size());
        oldCatalog.close();
        SatelliteCatalog jamxUpdated;
        assert(jamxUpdated.load(&error));
        assert(jamxUpdated.entries().size() == 1);
        const auto* profile = jamxUpdated.find(100470);
        assert(profile && profile->name == QStringLiteral("My JAMX"));
        assert((profile->frequenciesHz == QList<qint64>{435075000, 435175000}));
        assert(profile->selectedHz == (alreadyConfigured ? 435175000 : 435075000));
        assert(profile->frequencyNotes.value(435075000) == QStringLiteral("Custom note"));
        if (alreadyConfigured)
            assert(profile->frequencyNotes.value(435175000) == QStringLiteral("Custom CW"));
        auto profiles = jamxUpdated.entries();
        profiles[0].frequenciesHz.removeAll(435175000);
        profiles[0].selectedHz = 435075000;
        profiles[0].frequencyNotes.remove(435175000);
        jamxUpdated.setEntries(profiles);
        assert(jamxUpdated.save(&error));
        SatelliteCatalog jamxReloaded;
        assert(jamxReloaded.load(&error));
        assert(!jamxReloaded.find(100470)->frequenciesHz.contains(435175000));
    }
#else
    // Standalone builds load the user's catalog without injecting defaults.
    assert(catalog.entries().size() == 1);
    assert(catalog.find(98247)->selectedHz == 437500000);
    assert(!catalog.find(61781) && !catalog.find(100465));
#endif

    SatelliteProfile custom;
    custom.norad = 123456;
    custom.name = QStringLiteral("Custom satellite");
    custom.frequenciesHz = {145800000, 145900000};
    custom.selectedHz = 145900000;
    custom.frequencyNotes.insert(145800000, QStringLiteral("遥测 / Telemetry"));
    custom.frequencyNotes.insert(145900000, QStringLiteral("Voice"));
    QList<SatelliteProfile> entries = catalog.entries();
    for (int index = entries.size() - 1; index >= 0; --index) {
        if (entries.at(index).norad == custom.norad)
            entries.removeAt(index);
    }
    entries.append(custom);
    catalog.setEntries(entries);
    const bool savedCatalog = catalog.save(&error);
    assert(savedCatalog);
    SatelliteCatalog reloaded;
    const bool loaded = reloaded.load(&error);
    assert(loaded);
    const SatelliteProfile* saved = reloaded.find(custom.norad);
    assert(saved && saved->name == custom.name &&
           saved->selectedHz == custom.selectedHz &&
           saved->frequenciesHz == custom.frequenciesHz);
    assert(reloaded.entries().size() == catalog.entries().size());
    assert(saved->frequencyNotes == custom.frequencyNotes);
    assert(saved->frequencyLabel(145800000).contains(QStringLiteral("遥测")));
    const int beforeImport = reloaded.entries().size();
    const QList<SatelliteProfile> discovered{
        {custom.norad, QStringLiteral("Downloaded name"), {}, 0},
        {234567, QStringLiteral("New satellite"), {}, 0},
        {234567, QStringLiteral("Duplicate source"), {}, 0},
        {345678, QString(), {}, 0}};
    const bool merged = reloaded.mergeDiscovered(discovered, &error);
    assert(merged);
    const bool mergedAgain = reloaded.mergeDiscovered(discovered, &error);
    assert(mergedAgain);
    assert(reloaded.entries().size() == beforeImport + 2);
    SatelliteCatalog imported;
    const bool importedLoaded = imported.load(&error);
    assert(importedLoaded);
    assert(imported.entries().size() == beforeImport + 2);
    assert(imported.find(custom.norad)->name == custom.name);
    assert(imported.find(custom.norad)->selectedHz == custom.selectedHz);
    assert(imported.find(custom.norad)->frequenciesHz == custom.frequenciesHz);
    assert(imported.find(custom.norad)->frequencyNotes == custom.frequencyNotes);
    assert(imported.find(234567)->name == QStringLiteral("New satellite"));
    assert(imported.find(234567)->frequenciesHz.isEmpty());
    assert(imported.find(234567)->selectedHz == 0);
    assert(imported.find(345678)->name == QStringLiteral("NORAD 345678"));
    return 0;
}
