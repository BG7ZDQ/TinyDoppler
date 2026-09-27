#include "satellite_catalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <limits>
#include <utility>

namespace {
constexpr qint64 kMinimumFrequencyHz = 1000000;
constexpr qint64 kMaximumFrequencyHz = 10000000000LL;

bool jsonInteger(const QJsonValue& value, qint64* output)
{
    if (!value.isDouble())
        return false;
    const double candidate = value.toDouble();
    if (candidate != candidate || candidate < 0 ||
        candidate > static_cast<double>(std::numeric_limits<qint64>::max()))
        return false;
    const qint64 converted = static_cast<qint64>(candidate);
    if (static_cast<double>(converted) != candidate)
        return false;
    *output = converted;
    return true;
}
}

QString SatelliteCatalog::filePath()
{
    const QString configDirectory =
        QString::fromLocal8Bit(qgetenv("TINY_DOPPLER_CONFIG_DIR"));
    if (QDir::isAbsolutePath(configDirectory))
        return QDir(configDirectory).filePath(QStringLiteral("satellites.json"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("satellites.json"));
}

bool SatelliteCatalog::parseNorad(const QString& input, int* number)
{
    if (!number)
        return false;
    const QString value = input.trimmed().toUpper();
    bool ok = false;
    const int decimal = value.toInt(&ok);
    if (ok && decimal > 0 && value.at(0).isDigit()) {
        *number = decimal;
        return true;
    }
    if (value.size() != 5 || !value.at(0).isLetter() ||
        value.at(0) == QLatin1Char('I') || value.at(0) == QLatin1Char('O'))
        return false;
    const QChar letter = value.at(0);
    if (letter < QLatin1Char('A') || letter > QLatin1Char('Z'))
        return false;
    const int suffix = value.mid(1).toInt(&ok);
    if (!ok || value.mid(1).size() != 4) // toInt alone accepts whitespace.
        return false;
    for (int index = 1; index < 5; ++index) {
        if (!value.at(index).isDigit())
            return false;
    }
    int prefix = letter.unicode() - QLatin1Char('A').unicode() + 10;
    if (letter > QLatin1Char('I'))
        --prefix;
    if (letter > QLatin1Char('O'))
        --prefix;
    *number = prefix * 10000 + suffix;
    return true;
}

QString SatelliteCatalog::alpha5(int number)
{
    if (number < 100000 || number > 339999)
        return {};
    const int prefix = number / 10000;
    QChar letter(QLatin1Char('A').unicode() + prefix - 10);
    if (letter >= QLatin1Char('I'))
        letter = QChar(letter.unicode() + 1);
    if (letter >= QLatin1Char('O'))
        letter = QChar(letter.unicode() + 1);
    return letter + QStringLiteral("%1").arg(number % 10000, 4, 10, QLatin1Char('0'));
}

bool SatelliteCatalog::parse(const QByteArray& json,
                             QList<SatelliteProfile>* entries, QString* error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        !root.value(QStringLiteral("satellites")).isArray()) {
        if (error)
            *error = QStringLiteral("Unsupported satellite configuration format.");
        return false;
    }
    QList<SatelliteProfile> parsed;
    QSet<int> seen;
    const QJsonArray satellites = root.value(QStringLiteral("satellites")).toArray();
    for (const QJsonValue& value : satellites) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("A satellite entry is not an object.");
            return false;
        }
        const QJsonObject item = value.toObject();
        qint64 norad = 0;
        if (!jsonInteger(item.value(QStringLiteral("norad")), &norad) ||
            norad <= 0 || norad > std::numeric_limits<int>::max() ||
            seen.contains(static_cast<int>(norad))) {
            if (error) *error = QStringLiteral("Invalid or duplicate NORAD number.");
            return false;
        }
        SatelliteProfile profile;
        profile.norad = static_cast<int>(norad);
        profile.name = item.value(QStringLiteral("name")).toString().trimmed();
        if (profile.name.isEmpty() || profile.name.size() > 120 ||
            !item.value(QStringLiteral("frequenciesHz")).isArray()) {
            if (error) *error = QStringLiteral("Invalid satellite name or frequency list.");
            return false;
        }
        const QJsonArray frequencies =
            item.value(QStringLiteral("frequenciesHz")).toArray();
        for (const QJsonValue& frequency : frequencies) {
            qint64 hz = 0;
            if (!jsonInteger(frequency, &hz) || hz < kMinimumFrequencyHz ||
                hz > kMaximumFrequencyHz || profile.frequenciesHz.contains(hz)) {
                if (error) *error = QStringLiteral("Invalid or duplicate frequency.");
                return false;
            }
            profile.frequenciesHz.append(hz);
        }
        if (!jsonInteger(item.value(QStringLiteral("selectedHz")),
                         &profile.selectedHz) ||
            (profile.selectedHz != 0 &&
             !profile.frequenciesHz.contains(profile.selectedHz))) {
            if (error) *error = QStringLiteral("Selected frequency is not in the list.");
            return false;
        }
        seen.insert(profile.norad);
        parsed.append(profile);
    }
    *entries = parsed;
    return true;
}

bool SatelliteCatalog::load(QString* error)
{
    const QString path = filePath();
    QFile file(path);
    const bool userFileExists = file.exists();
    if (!userFileExists && !QFile::exists(QStringLiteral(":/tiny/default_satellites.json"))) {
        entries_.clear();
        return true;
    }
    if (!userFileExists)
        file.setFileName(QStringLiteral(":/tiny/default_satellites.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QList<SatelliteProfile> parsed;
    const QByteArray bytes = file.readAll();
    file.close();
    if (!parse(bytes, &parsed, error))
        return false;
    entries_ = std::move(parsed);
    if (userFileExists) {
        QFile defaults(QStringLiteral(":/tiny/default_satellites.json"));
        if (defaults.open(QIODevice::ReadOnly)) {
            const QByteArray defaultBytes = defaults.readAll();
            const int currentRevision = QJsonDocument::fromJson(bytes).object()
                                            .value(QStringLiteral("catalogRevision"))
                                            .toInt();
            const int defaultRevision = QJsonDocument::fromJson(defaultBytes).object()
                                            .value(QStringLiteral("catalogRevision"))
                                            .toInt();
            if (currentRevision < defaultRevision) {
                QList<SatelliteProfile> supplied;
                QString defaultError;
                if (!parse(defaultBytes, &supplied, &defaultError)) {
                    if (error) *error = defaultError;
                    return false;
                }
                // BY04's provisional SatNOGS ID was replaced by its cataloged
                // NORAD ID. Keep any frequencies selected by the user.
                if (currentRevision < 3) {
                    for (int index = 0; index < entries_.size(); ++index) {
                        SatelliteProfile& entry = entries_[index];
                        if (entry.norad != 98247 ||
                            (entry.name.compare(QStringLiteral("BY04"), Qt::CaseInsensitive) != 0 &&
                             entry.name.compare(QStringLiteral("BY70-4"), Qt::CaseInsensitive) != 0))
                            continue;
                        if (find(100469))
                            entries_.removeAt(index);
                        else
                            entry.norad = 100469;
                        break;
                    }
                }
                for (const SatelliteProfile& profile : supplied) {
                    if (!find(profile.norad))
                        entries_.append(profile);
                }
                if (!save(error))
                    return false;
            }
        }
    }
    return true;
}

bool SatelliteCatalog::save(QString* error) const
{
    const QString path = filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("Cannot create configuration directory.");
        return false;
    }
    QJsonArray satellites;
    for (const SatelliteProfile& profile : entries_) {
        QJsonArray frequencies;
        for (qint64 hz : profile.frequenciesHz)
            frequencies.append(static_cast<double>(hz));
        satellites.append(QJsonObject{
            {QStringLiteral("norad"), profile.norad},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("frequenciesHz"), frequencies},
            {QStringLiteral("selectedHz"), static_cast<double>(profile.selectedHz)}
        });
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("catalogRevision"), 3},
        {QStringLiteral("satellites"), satellites}
    }).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QStringList SatelliteCatalog::defaultSources()
{
    QFile defaults(QStringLiteral(":/tiny/default_satellites.json"));
    if (!defaults.open(QIODevice::ReadOnly))
        return {};
    const QJsonArray sources = QJsonDocument::fromJson(defaults.readAll()).object()
                                  .value(QStringLiteral("sources")).toArray();
    QStringList result;
    for (const auto& source : sources) {
        if (source.isString() && !source.toString().trimmed().isEmpty())
            result.append(source.toString().trimmed());
    }
    return result;
}

void SatelliteCatalog::setEntries(QList<SatelliteProfile> entries)
{
    entries_ = std::move(entries);
}

bool SatelliteCatalog::mergeDiscovered(const QList<SatelliteProfile>& discovered,
                                       QString* error)
{
    const auto previous = entries_;
    QSet<int> known;
    for (const auto& entry : entries_)
        known.insert(entry.norad);
    for (const auto& satellite : discovered) {
        if (satellite.norad <= 0 || known.contains(satellite.norad))
            continue;
        QString name = satellite.name.trimmed().left(120);
        if (name.isEmpty())
            name = QStringLiteral("NORAD %1").arg(satellite.norad);
        // Orbital elements do not provide a radio frequency.
        entries_.append({satellite.norad, name, {}, 0});
        known.insert(satellite.norad);
    }
    if (entries_.size() == previous.size() || save(error))
        return true;
    entries_ = previous;
    return false;
}

const SatelliteProfile* SatelliteCatalog::find(int norad) const
{
    for (const SatelliteProfile& entry : entries_) {
        if (entry.norad == norad)
            return &entry;
    }
    return nullptr;
}

bool SatelliteCatalog::selectFrequency(int norad, qint64 frequencyHz,
                                       QString* error)
{
    for (SatelliteProfile& entry : entries_) {
        if (entry.norad != norad)
            continue;
        if (frequencyHz < kMinimumFrequencyHz ||
            frequencyHz > kMaximumFrequencyHz) {
            if (error) *error = QStringLiteral("Frequency is outside the supported range.");
            return false;
        }
        const SatelliteProfile previous = entry;
        if (!entry.frequenciesHz.contains(frequencyHz))
            entry.frequenciesHz.append(frequencyHz);
        entry.selectedHz = frequencyHz;
        if (save(error))
            return true;
        entry = previous;
        return false;
    }
    if (error) *error = QStringLiteral("Satellite was not found.");
    return false;
}
