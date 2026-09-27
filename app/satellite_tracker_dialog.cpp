#include "satellite_tracker_dialog.h"
#include "catalog_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTextOption>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRotationRadSec = 7.2921150e-5;
constexpr double kLightKmSec = 299792.458;
constexpr quint32 kDopplerMagic = 0x504f4441U;

QString normalizedSatellite(QString value)
{
    value = value.toUpper();
    value.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
    value.remove(QStringLiteral("UV"));
    return value;
}

QString cachePath()
{
    const QString overridePath = QString::fromLocal8Bit(qgetenv("TINY_DOPPLER_CONFIG_DIR"));
    const QString root = QDir::isAbsolutePath(overridePath) ? overridePath :
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("ephemerides.json"));
}

QNetworkRequest orbitRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setRawHeader("User-Agent", "TinyDoppler/1.0");
    request.setTransferTimeout(15000);
    return request;
}

void boundReply(QNetworkReply* reply)
{
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(15000);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [reply](qint64 received, qint64 total) {
        constexpr qint64 maxBytes = 16 * 1024 * 1024;
        if (received > maxBytes || total > maxBytes)
            reply->abort();
    });
}

double jsonNumber(const QJsonObject& object, const QString& key, bool* ok)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        *ok = true;
        return value.toDouble();
    }
    if (value.isString())
        return value.toString().toDouble(ok);
    *ok = false;
    return 0.0;
}
}

SatelliteTrackerDialog::SatelliteTrackerDialog(
    double longitudeDeg, double latitudeDeg, double altitudeMeters,
    const QString& preferredSatellite, QWidget* parent, bool integrated)
    : QDialog(parent), longitudeDeg_(longitudeDeg), latitudeDeg_(latitudeDeg),
      altitudeMeters_(altitudeMeters), preferredSatellite_(preferredSatellite),
      integrated_(integrated)
{
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);
    setWindowTitle(QStringLiteral("Tiny Doppler"));
    setWindowIcon(QIcon(QStringLiteral(":/tiny/icon.png")));
    buildUi();
    QString catalogError;
    if (!catalog_.load(&catalogError)) {
        QMessageBox::critical(this, tr("无法读取卫星设置"),
                              tr("卫星配置文件无法读取：%1\n请检查文件后重试。")
                                  .arg(catalogError));
    } else if (!QFile::exists(SatelliteCatalog::filePath()) &&
               !catalog_.save(&catalogError)) {
        QMessageBox::critical(this, tr("无法保存卫星设置"),
                              tr("无法创建卫星配置文件：%1").arg(catalogError));
    }
    loadSettings();
    refreshSatelliteChoices();
    adjustSize();
    setMinimumSize(500, 530);
    resize(550, 560);

#ifdef Q_OS_WIN
    if (!QStandardPaths::isTestModeEnabled())
        mappingHandle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, 64, L"Local\\ASRTU_DOPPLER_CONTROL_V1");
    if (mappingHandle_)
        mappingView_ = static_cast<unsigned char*>(
            MapViewOfFile(mappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, 64));
#endif

    network_ = new QNetworkAccessManager(this);
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { updateTracking(); });
    timer_->start(500);

    QFile cache(cachePath());
    if (cache.open(QIODevice::ReadOnly)) {
        const QJsonDocument saved = QJsonDocument::fromJson(cache.readAll());
        if (saved.isArray()) {
            int index = 0;
            for (const auto& value : saved.array()) {
                if (value.isString())
                    cachedSources_.insert(QStringLiteral("legacy:%1").arg(index++),
                                          value.toString().toUtf8());
            }
        } else if (saved.object().value(QStringLiteral("schemaVersion")).toInt() == 2) {
            const auto payloads = saved.object().value(QStringLiteral("sources")).toObject();
            for (auto it = payloads.begin(); it != payloads.end(); ++it) {
                if (it.value().isString())
                    cachedSources_.insert(it.key(), it.value().toString().toUtf8());
            }
        }
        pruneEphemerisCache();
        if (installEphemerides(cachedSources_.values()))
            status_->setText(tr("已载入本地星历，正在更新…"));
    }
    if (!sourceUrls_.isEmpty())
        QTimer::singleShot(0, this, [this] { updateTle(); });
    else
        status_->setText(tr("没有星历来源"));
}

SatelliteTrackerDialog::~SatelliteTrackerDialog()
{
    saveSettings();
    publishDoppler(0, 0, false);
#ifdef Q_OS_WIN
    if (mappingView_)
        UnmapViewOfFile(mappingView_);
    if (mappingHandle_)
        CloseHandle(mappingHandle_);
#endif
}

void SatelliteTrackerDialog::buildUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background:#f5f8fc; font-size:10pt; }"
        "QWidget { color:#17202a; }"
        "QGroupBox { background:white; border:1px solid #dce5ef; "
        "border-radius:8px; }"
        "QLabel#sectionTitle { color:#17202a; font-weight:600; }"
        "QComboBox,QDoubleSpinBox { background:white; color:#17202a; "
        "border:1px solid #cbd5e1; min-height:25px; "
        "border-radius:5px; padding:3px 5px; }"
        "QPlainTextEdit { background:white; color:#17202a; "
        "border:1px solid #cbd5e1; border-radius:5px; padding:5px; }"
        "QComboBox QAbstractItemView { background:#ffffff; color:#17202a; "
        "selection-background-color:#dceeff; selection-color:#17202a; "
        "outline:0; }"
        "QComboBox QAbstractItemView::item:hover { background:#dceeff; "
        "color:#17202a; }"
        "QComboBox::drop-down { border:0; width:25px; }"
        "QComboBox::down-arrow { image:url(:/tiny/arrow.png); "
        "width:16px; height:16px; }"
        "QPushButton { min-height:32px; border:1px solid #a9c9ef; border-radius:6px; "
        "background:#edf5ff; color:#145ca8; padding:0 14px; }"
        "QLabel#value { color:#075db3; font-weight:600; }"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(18);

    auto* receivedBox = new QGroupBox(this);
    auto* receivedLayout = new QVBoxLayout(receivedBox);
    receivedLayout->setContentsMargins(14, 12, 14, 12);
    auto* receivedTitle = new QLabel(
        QCoreApplication::translate("ASRTU", "实时跟踪数据"), receivedBox);
    receivedTitle->setObjectName(QStringLiteral("sectionTitle"));
    receivedLayout->addWidget(receivedTitle);
    received_ = new QLabel(tr("等待星历数据"), receivedBox);
    received_->setWordWrap(false);
    received_->setAlignment(Qt::AlignCenter);
    received_->setObjectName(QStringLiteral("value"));
    receivedLayout->addWidget(received_);
    auto* values = new QGridLayout;
    auto addValue = [values, receivedBox](int row, int column, const QString& name,
                                          QLabel*& output) {
        values->addWidget(new QLabel(name, receivedBox), row, column * 2);
        output = new QLabel(QStringLiteral("--"), receivedBox);
        output->setObjectName(QStringLiteral("value"));
        values->addWidget(output, row, column * 2 + 1);
    };
    addValue(0, 0, QCoreApplication::translate("ASRTU", "方位角"), azimuth_);
    addValue(0, 1, QCoreApplication::translate("ASRTU", "仰角"), elevation_);
    addValue(1, 0, QCoreApplication::translate("ASRTU", "距离"), range_);
    addValue(1, 1, QCoreApplication::translate("ASRTU", "多普勒"), correction_);
    addValue(2, 0, QCoreApplication::translate("ASRTU", "接收频率"), downlink_);
    addValue(2, 1, tr("星历日期"), tleEpoch_);
    receivedLayout->addLayout(values);
    root->addWidget(receivedBox);

    auto* selectionBox = new QGroupBox(this);
    selectionBox->setMinimumHeight(195);
    auto* selection = new QFormLayout(selectionBox);
    selection->setContentsMargins(14, 12, 14, 12);
    selection->setHorizontalSpacing(12);
    selection->setVerticalSpacing(9);
    auto* selectionTitle = new QLabel(
        QCoreApplication::translate("ASRTU", "卫星与频率"), selectionBox);
    selectionTitle->setObjectName(QStringLiteral("sectionTitle"));
    selection->addRow(selectionTitle);
    satellite_ = new QComboBox(selectionBox);
    satellite_->setPlaceholderText(tr("未设置"));
    frequencyPreset_ = new QComboBox(selectionBox);
    frequencyMHz_ = new QDoubleSpinBox(selectionBox);
    frequencyMHz_->setRange(0.0, 10000.0);
    frequencyMHz_->setDecimals(6);
    frequencyMHz_->setSingleStep(0.001);
    frequencyMHz_->setSpecialValueText(tr("未设置"));
    frequencyMHz_->setSuffix(QStringLiteral(" MHz"));
    frequencyMHz_->setValue(0.0);
    selection->addRow(QCoreApplication::translate("ASRTU", "卫星"), satellite_);
    selection->addRow(QCoreApplication::translate("ASRTU", "频率预设"), frequencyPreset_);
    selection->addRow(QCoreApplication::translate("ASRTU", "标称下行"), frequencyMHz_);
    auto* manageButton = new QPushButton(tr("卫星与频率管理>"), selectionBox);
    selection->addRow(QString(), manageButton);
    root->addWidget(selectionBox);

    auto* sourceBox = new QGroupBox(this);
    auto* sourceLayout = new QVBoxLayout(sourceBox);
    sourceLayout->setContentsMargins(14, 12, 14, 12);
    auto* sourceTitle = new QLabel(tr("星历"), sourceBox);
    sourceTitle->setObjectName(QStringLiteral("sectionTitle"));
    sourceLayout->addWidget(sourceTitle);
    auto* sourceActions = new QHBoxLayout;
    status_ = new QLabel(QCoreApplication::translate("ASRTU", "等待更新"), sourceBox);
    status_->setWordWrap(true);
    updateButton_ = new QPushButton(tr("更新星历"), sourceBox);
    auto* sourceButton = new QPushButton(tr("星历管理"), sourceBox);
    sourceLayout->addWidget(status_);
    sourceActions->addStretch(1);
    sourceActions->addWidget(sourceButton);
    sourceActions->addWidget(updateButton_);
    sourceLayout->addLayout(sourceActions);
    root->addWidget(sourceBox);

    auto* stationRow = new QHBoxLayout;
    station_ = new QLabel(this);
    station_->setStyleSheet(QStringLiteral("color:#667788;"));
    refreshStationLabel();
    stationRow->addWidget(station_, 1);
    if (!integrated_) {
        auto* editStationButton = new QPushButton(tr("设置地面站"), this);
        editStationButton->setStyleSheet(QStringLiteral(
            "QPushButton { min-height:25px; padding:0 8px; }"));
        stationRow->addWidget(editStationButton);
        connect(editStationButton, &QPushButton::clicked, this,
                [this] { editStation(); });
    }
    root->addLayout(stationRow);

    connect(updateButton_, &QPushButton::clicked, this, [this] { updateTle(); });
    connect(manageButton, &QPushButton::clicked, this, [this] { editCatalog(); });
    connect(sourceButton, &QPushButton::clicked, this, [this] { editSources(); });
    connect(satellite_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
                refreshFrequencyPresets(true);
                updateTracking();
            });
    connect(frequencyPreset_, qOverload<int>(&QComboBox::activated),
            this, [this](int index) {
                const QVariant value = frequencyPreset_->itemData(index);
                if (value.isValid()) {
                    frequencyMHz_->setValue(value.toDouble());
                    QString error;
                    if (!catalog_.selectFrequency(satellite_->currentData().toInt(),
                                                  qRound64(value.toDouble() * 1e6),
                                                  &error))
                        QMessageBox::critical(this, tr("无法保存频率"), error);
                    updateTracking();
                }
            });
    connect(frequencyMHz_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { updateTracking(); });
    connect(frequencyMHz_, &QDoubleSpinBox::editingFinished, this, [this] {
        const int norad = satellite_->currentData().toInt();
        if (!norad || frequencyMHz_->value() <= 0.0)
            return;
        QString error;
        if (!catalog_.selectFrequency(norad,
                                      qRound64(frequencyMHz_->value() * 1e6),
                                      &error)) {
            QMessageBox::critical(this, tr("无法保存频率"), error);
            return;
        }
        refreshFrequencyPresets(false);
    });
}

void SatelliteTrackerDialog::loadSettings()
{
    QSettings settings(QStandardPaths::isTestModeEnabled() ? QSettings::IniFormat
                                                         : QSettings::NativeFormat,
                       QSettings::UserScope, QStringLiteral("TinyDoppler"),
                       integrated_ ? QStringLiteral("Tracker")
                                   : QStringLiteral("StandaloneTracker"));
    sourceUrls_ = settings.value(QStringLiteral("tle_sources"),
                                SatelliteCatalog::defaultSources()).toStringList();
    for (const auto& value : settings.value(QStringLiteral("dismissed_norads")).toStringList()) {
        bool ok = false;
        const int norad = value.toInt(&ok);
        if (ok && norad > 0)
            dismissedNorads_.insert(norad);
    }
    preferredSatellite_ = settings.value(QStringLiteral("satellite"),
                                         preferredSatellite_).toString();
    if (!integrated_)
        return;
    QSettings legacy(QStringLiteral("ASRTU"), QStringLiteral("AstroSeriesTracker"));

    // The legacy tracker stored one global frequency. Migrate it only to
    // the satellite selected there, never to every newly selected satellite.
    if (!settings.value(QStringLiteral("legacy_frequency_migrated"), false).toBool() &&
        legacy.contains(QStringLiteral("frequency_mhz"))) {
        const QString legacyName =
            legacy.value(QStringLiteral("satellite")).toString();
        const qint64 hz = qRound64(
            legacy.value(QStringLiteral("frequency_mhz")).toDouble() * 1e6);
        for (const SatelliteProfile& profile : catalog_.entries()) {
            const QString currentName = normalizedSatellite(profile.name);
            const QString oldName = normalizedSatellite(legacyName);
            if (!oldName.isEmpty() && hz >= 1000000 &&
                (currentName.contains(oldName) || oldName.contains(currentName))) {
                QString error;
                if (!catalog_.selectFrequency(profile.norad, hz, &error))
                    QMessageBox::warning(this, tr("旧设置未迁移"), error);
                break;
            }
        }
        settings.setValue(QStringLiteral("legacy_frequency_migrated"), true);
    }
}

void SatelliteTrackerDialog::saveSettings()
{
    QSettings settings(QStandardPaths::isTestModeEnabled() ? QSettings::IniFormat
                                                         : QSettings::NativeFormat,
                       QSettings::UserScope, QStringLiteral("TinyDoppler"),
                       integrated_ ? QStringLiteral("Tracker")
                                   : QStringLiteral("StandaloneTracker"));
    settings.setValue(QStringLiteral("tle_sources"), sourceUrls_);
    QStringList dismissed;
    for (int norad : dismissedNorads_)
        dismissed.append(QString::number(norad));
    settings.setValue(QStringLiteral("dismissed_norads"), dismissed);
    settings.setValue(QStringLiteral("satellite"), satellite_->currentText());
    settings.setValue(QStringLiteral("longitude"), longitudeDeg_);
    settings.setValue(QStringLiteral("latitude"), latitudeDeg_);
    settings.setValue(QStringLiteral("altitude"), altitudeMeters_);
}

void SatelliteTrackerDialog::refreshStationLabel()
{
    station_->setText(
        QCoreApplication::translate("ASRTU", "地面站：%1°, %2°，%3 m")
            .arg(longitudeDeg_, 0, 'f', 5)
            .arg(latitudeDeg_, 0, 'f', 5)
            .arg(altitudeMeters_, 0, 'f', 1));
}

void SatelliteTrackerDialog::editStation()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("地面站位置"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* longitude = new QDoubleSpinBox(&dialog);
    auto* latitude = new QDoubleSpinBox(&dialog);
    auto* altitude = new QDoubleSpinBox(&dialog);
    longitude->setRange(-180.0, 180.0);
    latitude->setRange(-90.0, 90.0);
    altitude->setRange(-500.0, 10000.0);
    longitude->setDecimals(6);
    latitude->setDecimals(6);
    altitude->setDecimals(1);
    longitude->setSuffix(QStringLiteral("°"));
    latitude->setSuffix(QStringLiteral("°"));
    altitude->setSuffix(QStringLiteral(" m"));
    longitude->setValue(longitudeDeg_);
    latitude->setValue(latitudeDeg_);
    altitude->setValue(altitudeMeters_);
    form->addRow(tr("经度"), longitude);
    form->addRow(tr("纬度"), latitude);
    form->addRow(tr("海拔"), altitude);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    longitudeDeg_ = longitude->value();
    latitudeDeg_ = latitude->value();
    altitudeMeters_ = altitude->value();
    refreshStationLabel();
    saveSettings();
    updateTracking();
}

void SatelliteTrackerDialog::updateTle()
{
    if (downloading_ || lookupReply_)
        return;
    if (sourceUrls_.isEmpty()) {
        editSources();
        if (sourceUrls_.isEmpty())
            return;
    }
    saveSettings();
    downloadSources_ = sourceUrls_;
    for (QString& source : downloadSources_)
        source = source.trimmed();
    downloadIndex_ = 0;
    successfulDownloads_ = 0;
    downloadErrors_.clear();
    freshSources_.clear();
    pruneEphemerisCache();
    downloading_ = true;
    updateButton_->setEnabled(false);
    downloadNextSource();
}

void SatelliteTrackerDialog::downloadNextSource()
{
    if (downloadIndex_ >= downloadSources_.size()) {
        finishDownloads();
        return;
    }
    const QString source = downloadSources_.at(downloadIndex_++);
    status_->setText(tr("正在更新星历（%1/%2）")
                         .arg(downloadIndex_)
                         .arg(downloadSources_.size()));
    QNetworkReply* reply = network_->get(orbitRequest(QUrl(source)));
    boundReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, source] {
        const QByteArray data = reply->readAll();
        QVector<Satellite> parsed;
        QString parseError;
        const bool success = reply->error() == QNetworkReply::NoError &&
                             parseEphemeris(data, &parsed, &parseError);
        const QString networkError = reply->errorString();
        const bool networkSucceeded = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (success && sourceUrls_.contains(source)) {
            cachedSources_.insert(source, data);
            freshSources_.insert(source, data);
            ++successfulDownloads_;
        } else if (!success) {
            downloadErrors_.append(QStringLiteral("%1: %2")
                                       .arg(source, networkSucceeded
                                                        ? parseError
                                                        : networkError));
        }
        downloadNextSource();
    });
}

void SatelliteTrackerDialog::finishDownloads()
{
    downloading_ = false;
    updateButton_->setEnabled(true);
    QTimer::singleShot(0, this, [this] { startNextEphemerisLookup(); });
    pruneEphemerisCache();
    QList<QByteArray> discoveries;
    for (auto it = freshSources_.cbegin(); it != freshSources_.cend(); ++it) {
        if (sourceUrls_.contains(it.key()))
            discoveries.append(it.value());
    }
    if (successfulDownloads_ == 0 ||
        !installEphemerides(cachedSources_.values(), discoveries)) {
        status_->setText(satellites_.isEmpty()
                             ? tr("星历更新失败")
                             : tr("更新失败，继续使用本地星历"));
        QMessageBox::warning(this, tr("星历更新失败"),
                             downloadErrors_.isEmpty()
                                 ? tr("下载的星历无法解析，请检查来源。")
                                 : downloadErrors_.join(QLatin1Char('\n')));
        return;
    }
    saveEphemerisCache();
    status_->setText(tr("星历更新完成：%1/%2 个来源")
                         .arg(successfulDownloads_)
                         .arg(downloadSources_.size()));
    if (!downloadErrors_.isEmpty())
        QMessageBox::warning(this, tr("部分星历未更新"),
                             downloadErrors_.join(QLatin1Char('\n')));
    const int wantedNorad = satellite_->currentData().toInt();
    if (wantedNorad > 0 &&
        std::none_of(satellites_.cbegin(), satellites_.cend(),
                     [wantedNorad](const Satellite& sat) {
                         return sat.tle.norad_id == wantedNorad;
                     })) {
        QMessageBox::warning(this, tr("星历更新失败"),
                             tr("星历中没有找到指定的卫星。"));
    }
}

void SatelliteTrackerDialog::pruneEphemerisCache()
{
    for (auto it = cachedSources_.begin(); it != cachedSources_.end();) {
        // Legacy caches have no source URL. Keep them for offline tracking,
        // but cached data must never rediscover deleted satellites.
        if (!it.key().startsWith(QStringLiteral("legacy:")) &&
            !sourceUrls_.contains(it.key()))
            it = cachedSources_.erase(it);
        else
            ++it;
    }
}

void SatelliteTrackerDialog::saveEphemerisCache()
{
    QJsonObject sources;
    for (auto it = cachedSources_.cbegin(); it != cachedSources_.cend(); ++it)
        sources.insert(it.key(), QString::fromUtf8(it.value()));
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("sources"), sources}
    }).toJson();
    QSaveFile cache(cachePath());
    if (!cache.open(QIODevice::WriteOnly) ||
        cache.write(bytes) != bytes.size() || !cache.commit())
        QMessageBox::warning(this, tr("无法保存星历"), cache.errorString());
}

QString SatelliteTrackerDialog::celestrakSource(int norad) const
{
    for (const auto& source : sourceUrls_) {
        const QUrl url(source);
        if (url.host().compare(QStringLiteral("celestrak.org"), Qt::CaseInsensitive) == 0 &&
            url.path() == QStringLiteral("/NORAD/elements/gp.php") &&
            QUrlQuery(url).queryItemValue(QStringLiteral("CATNR")).toInt() == norad)
            return source;
    }
    return QStringLiteral(
        "https://celestrak.org/NORAD/elements/gp.php?CATNR=%1&FORMAT=JSON").arg(norad);
}

void SatelliteTrackerDialog::queueEphemerisLookup(int norad)
{
    if (norad <= 0 || pendingLookups_.contains(norad) || !catalog_.find(norad))
        return;
    const QString source = celestrakSource(norad);
    if (!sourceUrls_.contains(source))
        sourceUrls_.append(source);
    // The source is visible and persistent even before a queued request starts.
    saveSettings();
    pendingLookups_.insert(norad);
    lookupQueue_.enqueue(norad);
    startNextEphemerisLookup();
}

void SatelliteTrackerDialog::startNextEphemerisLookup()
{
    if (downloading_ || lookupReply_)
        return;
    while (!lookupQueue_.isEmpty() && !catalog_.find(lookupQueue_.head()))
        pendingLookups_.remove(lookupQueue_.dequeue());
    if (lookupQueue_.isEmpty())
        return;
    const int norad = lookupQueue_.dequeue();
    const QString name = catalog_.find(norad)->name;
    const QString source = celestrakSource(norad);
    if (!sourceUrls_.contains(source)) {
        pendingLookups_.remove(norad);
        QTimer::singleShot(0, this, [this] { startNextEphemerisLookup(); });
        return;
    }
    status_->setText(tr("正在查询 %1 的星历…").arg(name));
    updateButton_->setEnabled(false);
    QNetworkReply* reply = network_->get(orbitRequest(QUrl(source)));
    lookupReply_ = reply;
    boundReply(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, norad, name, source] {
        const QByteArray data = reply->readAll();
        const bool networkOk = reply->error() == QNetworkReply::NoError;
        QString error;
        const bool found = networkOk && validateEphemeris(data, norad, &error);
        const QString reason = networkOk ? error : reply->errorString();
        lookupReply_ = nullptr;
        pendingLookups_.remove(norad);
        reply->deleteLater();
        updateButton_->setEnabled(true);
        // The user may have removed or renumbered the satellite in the meantime.
        if (catalog_.find(norad) && sourceUrls_.contains(source)) {
            if (found) {
                cachedSources_.insert(source, data);
                installEphemerides(cachedSources_.values(), {data});
                saveEphemerisCache();
                saveSettings();
                status_->setText(tr("已添加 %1 的星历").arg(name));
            } else {
                status_->setText(tr("%1 的星历下载失败，来源已保留").arg(name));
                QMessageBox warning(QMessageBox::Warning, tr("星历更新失败"),
                    tr("无法下载 %1 的星历。\n来源已保留，可点击“更新星历”重试。\n\n%2\n%3")
                        .arg(name, source, reason), QMessageBox::Ok, this);
                warning.setTextFormat(Qt::PlainText);
                warning.exec();
            }
        } else {
            status_->setText(QCoreApplication::translate("ASRTU", "等待更新"));
        }
        QTimer::singleShot(0, this, [this] { startNextEphemerisLookup(); });
    });
}

bool SatelliteTrackerDialog::validateEphemeris(const QByteArray& payload,
                                               int expectedNorad,
                                               QString* error)
{
    QVector<Satellite> parsed;
    if (!parseEphemeris(payload, &parsed, error))
        return false;
    for (const Satellite& sat : parsed) {
        if (sat.tle.norad_id == expectedNorad)
            return true;
    }
    if (error)
        *error = tr("星历中没有找到指定的卫星。");
    return false;
}

bool SatelliteTrackerDialog::parseEphemeris(const QByteArray& payload,
                                            QVector<Satellite>* parsed,
                                            QString* error)
{
    const QByteArray trimmed = payload.trimmed();
    if (trimmed.startsWith('[') || trimmed.startsWith('{')) {
        QJsonParseError jsonError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &jsonError);
        if (document.isNull()) {
            if (error) *error = tr("JSON 星历格式有误：%1").arg(jsonError.errorString());
            return false;
        }
        QJsonArray objects;
        if (document.isArray())
            objects = document.array();
        else if (document.isObject())
            objects.append(document.object());
        for (const QJsonValue& value : objects) {
            if (!value.isObject())
                continue;
            const QJsonObject item = value.toObject();
            int norad = 0;
            const QJsonValue id = item.value(QStringLiteral("NORAD_CAT_ID"));
            const QString idText = id.isString()
                ? id.toString() : QString::number(id.toInt());
            if (!SatelliteCatalog::parseNorad(idText, &norad))
                continue;
            bool ok = false;
            Satellite sat{};
            sat.tle.norad_id = norad;
            sat.name = item.value(QStringLiteral("OBJECT_NAME")).toString().trimmed();
            if (sat.name.isEmpty())
                sat.name = QStringLiteral("NORAD %1").arg(norad);
            const QByteArray name = sat.name.toUtf8().left(SGP4_TLE_NAME_LEN - 1);
            std::memcpy(sat.tle.name, name.constData(),
                        static_cast<std::size_t>(name.size()));
            QString epochText = item.value(QStringLiteral("EPOCH")).toString();
            if (epochText.contains(QLatin1Char('.'))) {
                const int dot = epochText.indexOf(QLatin1Char('.'));
                epochText = epochText.left(dot + 4);
            }
            if (!epochText.endsWith(QLatin1Char('Z')))
                epochText.append(QLatin1Char('Z'));
            const QDateTime epoch = QDateTime::fromString(epochText,
                                                         Qt::ISODateWithMs);
            if (!epoch.isValid())
                continue;
            sat.tle.epoch_jd = sgp4_unix_to_jd(
                epoch.toMSecsSinceEpoch() / 1000.0);
            sat.tle.mean_motion =
                jsonNumber(item, QStringLiteral("MEAN_MOTION"), &ok);
            if (!ok || !std::isfinite(sat.tle.mean_motion) ||
                sat.tle.mean_motion <= 0.0)
                continue;
            sat.tle.eccentricity =
                jsonNumber(item, QStringLiteral("ECCENTRICITY"), &ok);
            if (!ok || !std::isfinite(sat.tle.eccentricity) ||
                sat.tle.eccentricity < 0.0 || sat.tle.eccentricity >= 1.0)
                continue;
            sat.tle.inclination =
                jsonNumber(item, QStringLiteral("INCLINATION"), &ok);
            if (!ok || !std::isfinite(sat.tle.inclination) ||
                sat.tle.inclination < 0.0 || sat.tle.inclination > 180.0)
                continue;
            sat.tle.raan =
                jsonNumber(item, QStringLiteral("RA_OF_ASC_NODE"), &ok);
            if (!ok || !std::isfinite(sat.tle.raan))
                continue;
            sat.tle.arg_perigee =
                jsonNumber(item, QStringLiteral("ARG_OF_PERICENTER"), &ok);
            if (!ok || !std::isfinite(sat.tle.arg_perigee))
                continue;
            sat.tle.mean_anomaly =
                jsonNumber(item, QStringLiteral("MEAN_ANOMALY"), &ok);
            if (!ok || !std::isfinite(sat.tle.mean_anomaly))
                continue;
            sat.tle.bstar = jsonNumber(item, QStringLiteral("BSTAR"), &ok);
            if (!ok || !std::isfinite(sat.tle.bstar))
                continue;
            sgp4_elements_t elements{};
            if (sgp4_tle_to_elements(&sat.tle, &elements) == SGP4_SUCCESS &&
                sgp4_init(&sat.state, &elements) == SGP4_SUCCESS)
                parsed->append(sat);
        }
        if (parsed->isEmpty() && error)
            *error = tr("没有找到可用的轨道数据。");
        return !parsed->isEmpty();
    }

    const QList<QByteArray> rawLines = payload.split('\n');
    QVector<QByteArray> lines;
    for (QByteArray line : rawLines) {
        line = line.trimmed();
        if (!line.isEmpty())
            lines.push_back(line);
    }
    for (int i = 0; i + 1 < lines.size(); ++i) {
        QByteArray name;
        QByteArray line1;
        QByteArray line2;
        if (lines[i].startsWith("1 ") && lines[i + 1].startsWith("2 ")) {
            line1 = lines[i];
            line2 = lines[i + 1];
            name = QByteArray("NORAD ") + line1.mid(2, 5);
            i += 1;
        } else if (i + 2 < lines.size() && lines[i + 1].startsWith("1 ") &&
                   lines[i + 2].startsWith("2 ")) {
            name = lines[i];
            line1 = lines[i + 1];
            line2 = lines[i + 2];
            i += 2;
        } else {
            continue;
        }
        Satellite sat;
        const QByteArray clippedName = name.left(SGP4_TLE_NAME_LEN - 1);
        const sgp4_error_t parseResult = sgp4_parse_tle_3line(
            clippedName.constData(), line1.constData(), line2.constData(), &sat.tle);
        sgp4_elements_t elements{};
        if (parseResult != SGP4_SUCCESS ||
            sgp4_tle_to_elements(&sat.tle, &elements) != SGP4_SUCCESS ||
            sgp4_init(&sat.state, &elements) != SGP4_SUCCESS)
            continue;
        sat.name = QString::fromUtf8(name);
        parsed->push_back(sat);
    }
    if (parsed->isEmpty() && error)
        *error = tr("没有找到可用的 TLE 星历；请检查编号、校验和与数据格式。");
    return !parsed->isEmpty();
}

bool SatelliteTrackerDialog::installEphemerides(const QList<QByteArray>& sources,
                                               const QList<QByteArray>& discoveries)
{
    QVector<Satellite> combined;
    for (const QByteArray& source : sources) {
        QVector<Satellite> parsed;
        QString error;
        if (!parseEphemeris(source, &parsed, &error))
            continue;
        for (const Satellite& sat : parsed) {
            const auto existing = std::find_if(
                combined.begin(), combined.end(), [&sat](const Satellite& item) {
                    return item.tle.norad_id == sat.tle.norad_id;
                });
            if (existing == combined.end())
                combined.append(sat);
            else if (sat.tle.epoch_jd > existing->tle.epoch_jd)
                *existing = sat;
        }
    }
    QList<SatelliteProfile> discovered;
    for (const auto& payload : discoveries) {
        QVector<Satellite> parsed;
        QString error;
        if (!parseEphemeris(payload, &parsed, &error))
            continue;
        for (const auto& sat : parsed) {
            if (!dismissedNorads_.contains(sat.tle.norad_id))
                discovered.append({sat.tle.norad_id, sat.name, {}, 0});
        }
    }
    QString catalogError;
    if (!catalog_.mergeDiscovered(discovered, &catalogError))
        QMessageBox::warning(this, tr("无法保存卫星设置"), catalogError);
    satellites_ = combined;
    refreshSatelliteChoices();
    return !combined.isEmpty();
}

void SatelliteTrackerDialog::refreshFrequencyPresets(bool chooseDefault)
{
    Q_UNUSED(chooseDefault)
    const SatelliteProfile* profile =
        catalog_.find(satellite_->currentData().toInt());
    frequencyPreset_->blockSignals(true);
    frequencyPreset_->clear();
    if (profile) {
        for (qint64 hz : profile->frequenciesHz) {
            frequencyPreset_->addItem(
                QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', 6),
                hz / 1e6);
        }
    }
    if (frequencyPreset_->count() == 0) {
        frequencyPreset_->addItem(tr("未设置"));
        frequencyPreset_->setEnabled(false);
        frequencyMHz_->setValue(0.0);
    } else {
        frequencyPreset_->setEnabled(true);
        const qint64 selected = profile->selectedHz;
        int index = profile->frequenciesHz.indexOf(selected);
        if (index < 0)
            index = 0;
        frequencyPreset_->setCurrentIndex(index);
        frequencyMHz_->setValue(profile->frequenciesHz.at(index) / 1e6);
    }
    frequencyPreset_->blockSignals(false);
}

void SatelliteTrackerDialog::selectPreferredSatellite()
{
    const QString wanted = normalizedSatellite(preferredSatellite_);
    for (int i = 0; i < satellite_->count(); ++i) {
        const QString candidate = normalizedSatellite(satellite_->itemText(i));
        if (!wanted.isEmpty() && (candidate.contains(wanted) || wanted.contains(candidate))) {
            satellite_->setCurrentIndex(i);
            return;
        }
    }
    if (satellite_->count() > 0)
        satellite_->setCurrentIndex(0);
}

void SatelliteTrackerDialog::refreshSatelliteChoices(int preferredNorad)
{
    if (preferredNorad == 0)
        preferredNorad = satellite_->currentData().toInt();
    satellite_->blockSignals(true);
    satellite_->clear();
    for (const SatelliteProfile& profile : catalog_.entries())
        satellite_->addItem(profile.name, profile.norad);
    int selected = -1;
    for (int index = 0; index < satellite_->count(); ++index) {
        if (satellite_->itemData(index).toInt() == preferredNorad) {
            selected = index;
            break;
        }
    }
    if (selected >= 0)
        satellite_->setCurrentIndex(selected);
    else
        selectPreferredSatellite();
    satellite_->blockSignals(false);
    refreshFrequencyPresets(false);
    updateTracking();
}

void SatelliteTrackerDialog::editCatalog()
{
    const int selectedNorad = satellite_->currentData().toInt();
    QSet<int> previous;
    for (const auto& entry : catalog_.entries())
        previous.insert(entry.norad);
    CatalogDialog dialog(catalog_, this);
    if (dialog.exec() == QDialog::Accepted) {
        for (int norad : previous) {
            if (!catalog_.find(norad))
                dismissedNorads_.insert(norad);
        }
        for (const auto& entry : catalog_.entries()) {
            if (!previous.contains(entry.norad))
                dismissedNorads_.remove(entry.norad);
        }
        saveSettings();
        refreshSatelliteChoices(selectedNorad);
        for (const auto& entry : catalog_.entries()) {
            // A cached orbit does not imply a configured download source.
            // Always refresh newly saved satellites, including explicit re-adds.
            if (!previous.contains(entry.norad))
                queueEphemerisLookup(entry.norad);
        }
    }
}

void SatelliteTrackerDialog::editSources()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("星历管理"));
    dialog.setMinimumSize(480, 300);
    dialog.resize(550, 330);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("每行填写一个下载网址。"), &dialog));
    auto* editor = new QPlainTextEdit(&dialog);
    const QStringList originalSources = sourceUrls_;
    editor->setPlainText(sourceUrls_.join(QLatin1Char('\n')));
    editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor->setWordWrapMode(QTextOption::WrapAnywhere);
    layout->addWidget(editor, 1);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QStringList lines = editor->toPlainText().split(
            QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        QStringList validated;
        for (const QString& line : lines) {
            const QString urlText = line.trimmed();
            const QUrl url(urlText);
            if (!url.isValid() ||
                (url.scheme() != QStringLiteral("https") &&
                 url.scheme() != QStringLiteral("http")) || url.host().isEmpty()) {
                QMessageBox::warning(&dialog, tr("网址无效"),
                                     tr("请检查星历来源：%1").arg(urlText));
                return;
            }
            if (!validated.contains(urlText))
                validated.append(urlText);
        }
        // Keep successful background discoveries that arrived while editing.
        for (const auto& source : sourceUrls_) {
            if (!originalSources.contains(source) && !validated.contains(source))
                validated.append(source);
        }
        sourceUrls_ = validated;
        pruneEphemerisCache();
        installEphemerides(cachedSources_.values());
        saveEphemerisCache();
        saveSettings();
        if (sourceUrls_.isEmpty())
            status_->setText(tr("没有星历来源"));
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog,
            &QDialog::reject);
    dialog.exec();
}

void SatelliteTrackerDialog::updateTracking()
{
    const int norad = satellite_->currentData().toInt();
    const SatelliteProfile* profile = catalog_.find(norad);
    const auto found = std::find_if(satellites_.cbegin(), satellites_.cend(),
                                    [norad](const Satellite& item) {
                                        return item.tle.norad_id == norad;
                                    });
    if (!profile || profile->selectedHz == 0 ||
        found == satellites_.cend()) {
        publishDoppler(0, 0, false);
        received_->setText(!profile ? tr("请添加卫星或下载星历") :
                           profile->selectedHz == 0
                               ? tr("请设置下行频率")
                               : tr("等待所选卫星的星历"));
        azimuth_->setText(QStringLiteral("--"));
        elevation_->setText(QStringLiteral("--"));
        range_->setText(QStringLiteral("--"));
        correction_->setText(QStringLiteral("--"));
        downlink_->setText(QStringLiteral("--"));
        tleEpoch_->setText(QStringLiteral("--"));
        return;
    }
    const Satellite& sat = *found;
    const double unixSeconds = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000.0;
    const double jd = sgp4_unix_to_jd(unixSeconds);
    sgp4_result_t result{};
    if (sgp4_propagate(&sat.state, (jd - sat.tle.epoch_jd) * 1440.0, &result) !=
        SGP4_SUCCESS) {
        status_->setText(tr("轨道计算失败"));
        publishDoppler(0, 0, false);
        return;
    }
    const double gst = sgp4_gstime(jd);
    const sgp4_vec3_t positionEci{result.r[0], result.r[1], result.r[2]};
    const sgp4_vec3_t velocityEci{result.v[0], result.v[1], result.v[2]};
    sgp4_vec3_t positionEcef{};
    sgp4_vec3_t velocityEcef{};
    sgp4_eci_to_ecef(&positionEci, gst, &positionEcef);
    sgp4_eci_to_ecef(&velocityEci, gst, &velocityEcef);
    velocityEcef.x += kEarthRotationRadSec * positionEcef.y;
    velocityEcef.y -= kEarthRotationRadSec * positionEcef.x;

    const sgp4_geodetic_t observer{
        latitudeDeg_ * kPi / 180.0,
        longitudeDeg_ * kPi / 180.0,
        altitudeMeters_ / 1000.0
    };
    sgp4_vec3_t observerEcef{};
    sgp4_geodetic_to_ecef(&observer, &observerEcef);
    sgp4_look_angles_t look{};
    sgp4_look_angles(&positionEcef, &observer, &look);
    const sgp4_vec3_t delta{positionEcef.x - observerEcef.x,
                            positionEcef.y - observerEcef.y,
                            positionEcef.z - observerEcef.z};
    const double rangeRateKmSec =
        (delta.x * velocityEcef.x + delta.y * velocityEcef.y +
         delta.z * velocityEcef.z) / std::max(1e-9, look.range_km);
    const double nominalHz = static_cast<double>(profile->selectedHz);
    const qint64 correctionHz = qRound64(-nominalHz * rangeRateKmSec / kLightKmSec);
    const qint64 targetHz = qRound64(nominalHz) + correctionHz;

    received_->setText(tr("%1 · %2 MHz")
                           .arg(sat.name)
                           .arg(targetHz / 1e6, 0, 'f', 6));
    azimuth_->setText(QStringLiteral("%1°").arg(sgp4_rad_to_deg(look.azimuth_rad), 0, 'f', 1));
    elevation_->setText(QStringLiteral("%1°").arg(sgp4_rad_to_deg(look.elevation_rad), 0, 'f', 1));
    range_->setText(QStringLiteral("%1 km").arg(look.range_km, 0, 'f', 1));
    correction_->setText(QStringLiteral("%1 Hz").arg(correctionHz));
    downlink_->setText(QStringLiteral("%1 MHz").arg(targetHz / 1e6, 0, 'f', 6));
    tleEpoch_->setText(QDateTime::fromMSecsSinceEpoch(
                          qRound64(sgp4_jd_to_unix(sat.tle.epoch_jd) * 1000.0), Qt::UTC)
                          .toString(QStringLiteral("MM-dd hh:mm")));
    publishDoppler(targetHz, correctionHz, true);
}

void SatelliteTrackerDialog::publishDoppler(qint64 targetHz, qint64 correctionHz,
                                             bool valid)
{
#ifdef Q_OS_WIN
    if (!mappingView_)
        return;
    std::memset(mappingView_, 0, 64);
    *reinterpret_cast<quint32*>(mappingView_ + 0) = kDopplerMagic;
    *reinterpret_cast<quint32*>(mappingView_ + 4) = 1;
    *reinterpret_cast<qint64*>(mappingView_ + 8) = targetHz;
    *reinterpret_cast<qint64*>(mappingView_ + 16) = correctionHz;
    *reinterpret_cast<qint64*>(mappingView_ + 24) = QDateTime::currentMSecsSinceEpoch();
    *reinterpret_cast<qint32*>(mappingView_ + 32) = valid ? 1 : 0;
    MemoryBarrier();
#else
    Q_UNUSED(targetHz)
    Q_UNUSED(correctionHz)
    Q_UNUSED(valid)
#endif
}
