#pragma once

#include <QList>
#include <QString>

struct SatelliteProfile {
    int norad = 0;
    QString name;
    QList<qint64> frequenciesHz;
    qint64 selectedHz = 0;
};

class SatelliteCatalog {
public:
    static QString filePath();
    static bool parseNorad(const QString& input, int* number);
    static QString alpha5(int number);

    bool load(QString* error);
    bool save(QString* error) const;
    const QList<SatelliteProfile>& entries() const { return entries_; }
    void setEntries(QList<SatelliteProfile> entries);
    const SatelliteProfile* find(int norad) const;
    bool selectFrequency(int norad, qint64 frequencyHz, QString* error);

private:
    static bool parse(const QByteArray& json, QList<SatelliteProfile>* entries,
                      QString* error);
    QList<SatelliteProfile> entries_;
};
