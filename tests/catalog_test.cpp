#include "satellite_catalog.h"

#include <QCoreApplication>
#include <QStandardPaths>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("TinyDopplerTest"));
    QStandardPaths::setTestModeEnabled(true);

    int number = 0;
    assert(SatelliteCatalog::parseNorad(QStringLiteral("A0123"), &number));
    assert(number == 100123);
    assert(SatelliteCatalog::parseNorad(QStringLiteral("100465"), &number));
    assert(number == 100465);
    assert(SatelliteCatalog::parseNorad(QStringLiteral("a0465"), &number));
    assert(number == 100465);
    assert(!SatelliteCatalog::parseNorad(QStringLiteral("A01234"), &number));
    assert(!SatelliteCatalog::parseNorad(QStringLiteral("I0000"), &number));
    assert(SatelliteCatalog::alpha5(100465) == QStringLiteral("A0465"));
    assert(SatelliteCatalog::alpha5(339999) == QStringLiteral("Z9999"));

    SatelliteCatalog catalog;
    QString error;
    assert(catalog.load(&error));
    const SatelliteProfile* asrtu = catalog.find(61781);
    assert(asrtu && asrtu->selectedHz == 435400000);
    const SatelliteProfile* jamx = catalog.find(100465);
    assert(jamx && jamx->frequenciesHz.contains(435075000));

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
    assert(catalog.save(&error));
    SatelliteCatalog reloaded;
    assert(reloaded.load(&error));
    const SatelliteProfile* saved = reloaded.find(custom.norad);
    assert(saved && saved->name == custom.name &&
           saved->selectedHz == custom.selectedHz &&
           saved->frequenciesHz == custom.frequenciesHz);
    assert(reloaded.find(61781) && reloaded.find(100465));
    return 0;
}
