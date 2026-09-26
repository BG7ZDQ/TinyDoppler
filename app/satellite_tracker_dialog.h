#pragma once

#include "satellite_catalog.h"

#include <QDialog>
#include <QVector>

#include "sgp4.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QTimer;

class SatelliteTrackerDialog final : public QDialog
{
    Q_OBJECT
public:
    SatelliteTrackerDialog(double longitudeDeg, double latitudeDeg,
                           double altitudeMeters, const QString& preferredSatellite,
                           QWidget* parent = nullptr);
    ~SatelliteTrackerDialog() override;
    static bool validateEphemeris(const QByteArray& payload,
                                  int expectedNorad, QString* error);

private:
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
    void finishDownloads();
    bool installEphemerides(const QList<QByteArray>& sources);
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
    SatelliteCatalog catalog_;
    QVector<Satellite> satellites_;
    QStringList downloadSources_;
    QStringList sourceUrls_;
    QList<QByteArray> downloadedSources_;
    int downloadIndex_ = 0;
    int successfulDownloads_ = 0;
    QStringList downloadErrors_;

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
    QTimer* timer_ = nullptr;

#ifdef Q_OS_WIN
    void* mappingHandle_ = nullptr;
    unsigned char* mappingView_ = nullptr;
#endif
};
