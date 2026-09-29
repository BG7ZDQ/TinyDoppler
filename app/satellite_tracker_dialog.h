#pragma once

#include "satellite_catalog.h"

#include <QDialog>
#include <QIcon>
#include <QVector>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <functional>

#include "sgp4.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QPushButton;
class QTimer;
class QUrl;

class SatelliteTrackerDialog final : public QDialog
{
    Q_OBJECT
public:
    // Replies must belong to the supplied parent and finish asynchronously.
    using RequestFactory =
        std::function<QNetworkReply*(const QNetworkRequest&, QObject*)>;
    SatelliteTrackerDialog(double longitudeDeg, double latitudeDeg,
                           double altitudeMeters, const QString& preferredSatellite,
                           QWidget* parent = nullptr, bool integrated = false,
                           RequestFactory requestFactory = {},
                           const QIcon& windowIcon = QIcon(QStringLiteral(":/tiny/icon.png")));
    ~SatelliteTrackerDialog() override;
    static bool validateEphemeris(const QByteArray& payload,
                                  int expectedNorad, QString* error);

private:
    friend struct TrackerTestAccess;
    struct Satellite {
        QString name;
        sgp4_tle_t tle{};
        sgp4_state_t state{};
    };

    void buildUi();
    void loadSettings();
    void saveSettings();
    void updateTle();
    void downloadNextSource();
    QNetworkReply* requestOrbit(const QUrl& url);
    void finishDownloads();
    void saveEphemerisCache();
    void pruneEphemerisCache();
    void queueEphemerisLookup(int norad);
    QString celestrakSource(int norad) const;
    void startNextEphemerisLookup();
    bool installEphemerides(const QList<QByteArray>& sources,
                            const QList<QByteArray>& discoveries = {});
    static bool parseEphemeris(const QByteArray& payload,
                               QVector<Satellite>* parsed, QString* error);
    void refreshFrequencyPresets(bool chooseDefault);
    void updateTracking();
    void selectPreferredSatellite();
    void refreshSatelliteChoices(int preferredNorad = 0);
    void editCatalog();
    void editSources();
    void editStation();
    void refreshStationLabel();
    void publishDoppler(qint64 targetHz, qint64 correctionHz, bool valid);

    double longitudeDeg_;
    double latitudeDeg_;
    double altitudeMeters_;
    QString preferredSatellite_;
    bool integrated_ = false;
    SatelliteCatalog catalog_;
    QVector<Satellite> satellites_;
    QStringList downloadSources_;
    QStringList sourceUrls_;
    int downloadIndex_ = 0;
    int successfulDownloads_ = 0;
    QStringList downloadErrors_;
    QMap<QString, QByteArray> cachedSources_;
    QMap<QString, QByteArray> freshSources_;
    QSet<int> dismissedNorads_;
    QQueue<int> lookupQueue_;
    QSet<int> pendingLookups_;
    bool downloading_ = false;
    QNetworkReply* lookupReply_ = nullptr;

    QComboBox* satellite_ = nullptr;
    QComboBox* frequencyPreset_ = nullptr;
    QDoubleSpinBox* frequencyMHz_ = nullptr;
    QLabel* received_ = nullptr;
    QLabel* azimuth_ = nullptr;
    QLabel* elevation_ = nullptr;
    QLabel* range_ = nullptr;
    QLabel* correction_ = nullptr;
    QLabel* downlink_ = nullptr;
    QLabel* tleEpoch_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* station_ = nullptr;
    QPushButton* updateButton_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    RequestFactory requestFactory_;
    QTimer* timer_ = nullptr;

#ifdef Q_OS_WIN
    void* mappingHandle_ = nullptr;
    unsigned char* mappingView_ = nullptr;
#endif
};
