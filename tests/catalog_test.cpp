#include "satellite_catalog.h"

#include <QCoreApplication>
#include <QFile>
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
    assert(fresh.find(61781) && fresh.find(100465) && fresh.find(100469));
    assert(SatelliteCatalog::defaultSources().size() == 3);
    for (const auto& source : SatelliteCatalog::defaultSources())
        assert(source.startsWith(QStringLiteral("https://celestrak.org/")));
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
    const SatelliteProfile* jamx = catalog.find(100465);
    assert(jamx && jamx->frequenciesHz.contains(435075000));
    const SatelliteProfile* by04 = catalog.find(100469);
    assert(by04 && by04->frequenciesHz.contains(437443000) &&
           by04->selectedHz == 437500000);
    assert(!catalog.find(98247));
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
    assert(imported.find(234567)->name == QStringLiteral("New satellite"));
    assert(imported.find(234567)->frequenciesHz.isEmpty());
    assert(imported.find(234567)->selectedHz == 0);
    assert(imported.find(345678)->name == QStringLiteral("NORAD 345678"));
    return 0;
}
