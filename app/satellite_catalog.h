#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct SatelliteProfile {
    int norad = 0;
    QString name;
    QList<qint64> frequenciesHz;
    qint64 selectedHz = 0;
    QMap<qint64, QString> frequencyNotes = {};
    QString ephemerisSource;

    bool operator==(const SatelliteProfile& other) const
    {
        return norad == other.norad && name == other.name &&
               frequenciesHz == other.frequenciesHz &&
               selectedHz == other.selectedHz &&
               frequencyNotes == other.frequencyNotes &&
               ephemerisSource == other.ephemerisSource;
    }
    bool operator!=(const SatelliteProfile& other) const
    {
        return !(*this == other);
    }

    QString frequencyLabel(qint64 hz) const
    {
        const QString frequency = QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', 6);
        const QString note = frequencyNotes.value(hz);
        return note.isEmpty() ? frequency : frequency + QStringLiteral(" · ") + note;
    }
};

class SatelliteCatalog {
public:
    // User data lives beside the receiver's main folder, not under AppData.
    static QString dataDirectory();
    static QString filePath();
    static QStringList defaultSources();
    static bool parseNorad(const QString& input, int* number);
    static QString alpha5(int number);

    bool load(QString* error);
    bool save(QString* error) const;
    const QList<SatelliteProfile>& entries() const { return entries_; }
    void setEntries(QList<SatelliteProfile> entries);
    const SatelliteProfile* find(int norad) const;
    bool selectFrequency(int norad, qint64 frequencyHz, QString* error);
    bool mergeDiscovered(const QList<SatelliteProfile>& discovered, QString* error);

private:
    static bool parse(const QByteArray& json, QList<SatelliteProfile>* entries,
                      QString* error);
    QList<SatelliteProfile> entries_;
};
