#include "satellite_tracker_dialog.h"

#include <QCoreApplication>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QByteArray omm = R"([
      {
        "OBJECT_NAME":"JAMX01",
        "NORAD_CAT_ID":100465,
        "EPOCH":"2026-09-24T21:07:15.413664",
        "MEAN_MOTION":15.09442172,
        "ECCENTRICITY":0.00140418,
        "INCLINATION":97.5383,
        "RA_OF_ASC_NODE":341.1626,
        "ARG_OF_PERICENTER":147.7761,
        "MEAN_ANOMALY":212.4329,
        "BSTAR":0.00033272472
      }
    ])";
    QString error;
    assert(SatelliteTrackerDialog::validateEphemeris(omm, 100465, &error));
    assert(!SatelliteTrackerDialog::validateEphemeris(omm, 100466, &error));
    assert(!SatelliteTrackerDialog::validateEphemeris(
        QByteArrayLiteral("[{\"NORAD_CAT_ID\":100465}]"), 100465, &error));
    return 0;
}
