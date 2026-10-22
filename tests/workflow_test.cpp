#include "satellite_tracker_dialog.h"
#include "catalog_dialog.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>
#include <functional>
#include <algorithm>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

static QByteArray orbit(int norad)
{
    return QStringLiteral(R"([{"OBJECT_NAME":"Downloaded name",
      "NORAD_CAT_ID":%1,"EPOCH":"2026-09-24T21:07:15.413664",
      "MEAN_MOTION":15.09442172,"ECCENTRICITY":0.00140418,
      "INCLINATION":97.5383,"RA_OF_ASC_NODE":341.1626,
      "ARG_OF_PERICENTER":147.7761,"MEAN_ANOMALY":212.4329,
      "BSTAR":0.00033272472}])").arg(norad).toUtf8();
}

struct Response {
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

class FakeReply final : public QNetworkReply {
public:
    static int liveCount;

    FakeReply(const QNetworkRequest& request, Response response, QObject* parent)
        : QNetworkReply(parent), body_(std::move(response.body))
    {
        ++liveCount;
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(10, this, [this, response] {
            if (isFinished()) return;
            if (response.error != NoError)
                setError(response.error, QStringLiteral("Test network failure"));
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    ~FakeReply() override
    {
        --liveCount;
    }
    void abort() override
    {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("Cancelled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return body_.size() - position_ + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char* data, qint64 size) override
    {
        const qint64 count = qMin(size, body_.size() - position_);
        if (count <= 0) return -1;
        std::memcpy(data, body_.constData() + position_, size_t(count));
        position_ += count;
        return count;
    }
private:
    QByteArray body_;
    qint64 position_ = 0;
};

int FakeReply::liveCount = 0;

class FakeNetwork final {
public:
    QMap<int, Response> responses;
    QList<QUrl> requests;
    QNetworkReply* get(const QNetworkRequest& request, QObject* parent)
    {
        requests.append(request.url());
        const QUrlQuery query(request.url());
        const QString group = query.queryItemValue("GROUP");
        int id = group == "active" ? 900 : group == "amateur" ? 901
            : query.queryItemValue("CATNR").toInt();
        if (id == 0)
            id = query.queryItemValue("norad_cat_id").toInt();
        return new FakeReply(request, responses.value(id, {"No GP data found"}), parent);
    }
};

static void awaitCondition(const std::function<bool()>& condition)
{
    QElapsedTimer timeout;
    timeout.start();
    while (!condition() && timeout.elapsed() < 3000) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    assert(condition());
}

static void capture(QWidget* widget, const char* name)
{
    const QString directory = qEnvironmentVariable("TINY_DOPPLER_QA_DIR");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    const bool saved = widget->grab().save(QDir(directory).filePath(QString::fromLatin1(name)));
    assert(saved);
}

struct TrackerTestAccess {
    static void checkLifetimes()
    {
        SatelliteCatalog catalog;
        catalog.setEntries({{61781, "ASRTU-1", {435400000}, 435400000}});
        QString error;
        assert(catalog.save(&error));
        QSettings settings(SatelliteTrackerDialog::settingsFilePath(false),
                           QSettings::IniFormat);
        settings.clear();
        settings.sync();
        FakeNetwork fake;
        fake.responses[61781] = {orbit(61781)};
        const auto factory = [&fake](const QNetworkRequest& request, QObject* parent) {
            return fake.get(request, parent);
        };
        const auto flushDeletes = [] {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            assert(FakeReply::liveCount == 0);
        };
        flushDeletes();
        for (int cycle = 0; cycle < 100; ++cycle) {
            QList<QPointer<QObject>> objects;
            {
                SatelliteTrackerDialog window(0, 0, 0, {}, nullptr, false, factory, QIcon());
                if (cycle % 3 == 0) {
                    window.updateTle();
                    assert(FakeReply::liveCount == 1);
                } else {
                    awaitCondition([&] { return !window.cachedSources_.isEmpty() &&
                        !window.downloading_; });
                    // Allow the scheduled startup update to run on cached starts.
                    QApplication::processEvents();
                    awaitCondition([&] { return !window.downloading_; });
                    flushDeletes();
                    if (cycle % 3 == 1) {
                        window.queueEphemerisLookup(61781);
                        assert(window.lookupReply_ && FakeReply::liveCount == 1);
                    } else {
                        const int objectCount = window.findChildren<QObject*>().size();
                        for (int tick = 0; tick < 1000; ++tick)
                            window.updateTracking();
                        for (auto edit : {&SatelliteTrackerDialog::editCatalog,
                                          &SatelliteTrackerDialog::editSources,
                                          &SatelliteTrackerDialog::editStation}) {
                            QTimer::singleShot(0, &window, [] {
                                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                                assert(dialog);
                                dialog->reject();
                            });
                            (window.*edit)();
                        }
                        assert(window.findChildren<QObject*>().size() == objectCount);
                        window.updateTle();
                        awaitCondition([&] { return !window.downloading_; });
                        flushDeletes();
                        assert(window.findChildren<QObject*>().size() == objectCount);
                    }
                }
                for (QObject* object : window.findChildren<QObject*>())
                    objects.append(QPointer<QObject>(object));
            }
            for (const auto& object : objects)
                assert(object.isNull());
            flushDeletes();
            fake.requests.clear();
        }
        SatelliteTrackerDialog window(0, 0, 0, {}, nullptr, false, factory, QIcon());
        for (int cycle = 0; cycle < 100; ++cycle) {
            const QString source = QStringLiteral(
                "https://celestrak.org/NORAD/elements/gp.php?CATNR=%1&FORMAT=JSON")
                .arg(20000 + cycle);
            assert(window.saveSources({source}, &error));
            assert(window.saveSources({}, &error));
            assert(window.sourceRevisions_.isEmpty());
        }
    }

    static void run()
    {
        FakeNetwork fake;
        const auto factory = [&fake](const QNetworkRequest& request, QObject* parent) {
            return fake.get(request, parent);
        };
        const QIcon icon = qEnvironmentVariableIsEmpty("TINY_DOPPLER_QA_DIR")
            ? QIcon() : QIcon(QStringLiteral(":/tiny/icon.png"));
        SatelliteTrackerDialog window(0, 0, 0, {}, nullptr, false, factory, icon);
        assert(window.catalog_.entries().isEmpty() && window.sourceUrls_.isEmpty());
        assert(!window.network_);
        QString error;
        QSettings persisted(SatelliteTrackerDialog::settingsFilePath(false),
                            QSettings::IniFormat);
        auto saveCatalog = [&](const QList<SatelliteProfile>& entries) {
            const bool ok = window.saveCatalogEntries(entries, &error);
            assert(ok);
            window.installEphemerides(window.cachedSources_.values());
        };
        auto saveSources = [&](const QStringList& sources) {
            const bool ok = window.saveSources(sources, &error);
            assert(ok);
            window.installEphemerides(window.cachedSources_.values());
        };
        QStringList warnings;
        QTimer closeWarnings;
        QObject::connect(&closeWarnings, &QTimer::timeout, [&] {
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (auto* warning = qobject_cast<QMessageBox*>(top)) {
                    if (warning->isVisible()) {
                        warnings.append(warning->text());
                        warning->accept();
                    }
                }
            }
        });
        closeWarnings.start(10);
        auto update = [&] {
            window.updateTle();
            awaitCondition([&] { return !window.downloading_; });
        };
        auto remove = [&](int id) {
            auto entries = window.catalog_.entries();
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                [id](const SatelliteProfile& p) { return p.norad == id; }), entries.end());
            saveCatalog(entries);
        };
        auto combined = [](int a, int b) {
            QJsonArray array = QJsonDocument::fromJson(orbit(a)).array();
            array.append(QJsonDocument::fromJson(orbit(b)).array().first());
            return QJsonDocument(array).toJson();
        };
        // Deletion from a TLE group preserves the other record byte-for-byte,
        // including Alpha-5 NORAD identifiers and line checksums.
        QByteArray line1 = "1 25544U 98067A   04236.56031392  .00020137  00000-0  16538-3 0  9993";
        QByteArray line2 = "2 25544  51.6335 344.7760 0007976 126.2523 325.9359 15.70406856328906";
        const QByteArray issTle = "ISS\n" + line1 + '\n' + line2 + '\n';
        line1.replace(2, 5, "A0465");
        line2.replace(2, 5, "A0465");
        line1[68] = char('0' + sgp4_calculate_checksum(line1.constData()));
        line2[68] = char('0' + sgp4_calculate_checksum(line2.constData()));
        const QByteArray jamxTle = "JAMX\n" + line1 + '\n' + line2 + '\n';
        assert(window.sourceNorads({}, issTle + jamxTle) == QSet<int>({25544, 100465}));
        assert(window.withoutSatellites(issTle + jamxTle, {25544}) == jamxTle);
        assert(window.withoutSatellites(issTle + jamxTle, {25544, 100465}).isEmpty());

        // Catalog Save must persist a refreshable source before any request.
        fake.responses[61781] = {orbit(61781)};
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            QTimer::singleShot(0, dialog, [] {
                auto* entry = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                entry->findChild<QLineEdit*>("satelliteName")->setText("My ASRTU");
                entry->findChild<QLineEdit*>("noradNumber")->setText("61781");
                entry->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            });
            dialog->findChild<QPushButton*>("addSatellite")->click();
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        window.editCatalog();
        const QString asrtu = window.celestrakSource(61781);
        persisted.sync();
        assert(persisted.value("tle_sources").toStringList().contains(asrtu));
        assert(window.catalog_.find(61781));
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(window.cachedSources_.value(asrtu) == orbit(61781));
        assert(window.satellites_.size() == 1);
        auto profiles = window.catalog_.entries();
        profiles[0].frequenciesHz = {435400000, 436210000};
        profiles[0].selectedHz = 436210000;
        profiles.append({100465, "Custom JAMX", {435500000}, 435500000});
        saveCatalog(profiles);
        window.refreshSatelliteChoices(61781);
        assert(window.frequencyMHz_->value() == 436.21);
        window.refreshSatelliteChoices(100465);
        assert(window.frequencyMHz_->value() == 435.5);

        // Failed automatic lookup keeps BOTH profile and saved retry source.
        fake.responses[100465] = {{}, QNetworkReply::TimeoutError};
        window.queueEphemerisLookup(100465);
        window.queueEphemerisLookup(100465);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(warnings.size() == 1 && warnings.last().contains("Test network failure"));
        assert(window.catalog_.find(100465));
        const QString jamx = window.celestrakSource(100465);
        assert(window.sourceUrls_.count(jamx) == 1);
        assert(!window.cachedSources_.contains(jamx));
        fake.responses[100465] = {orbit(61781)}; // Valid orbit, wrong satellite.
        window.queueEphemerisLookup(100465);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(warnings.size() == 2 && !window.cachedSources_.contains(jamx));
        fake.responses[100465] = {orbit(100465)};
        update();
        assert(window.satellites_.size() == 2);
        assert(window.catalog_.find(100465)->name == "Custom JAMX");

        // A shared source discovers all members after update.
        const QString group = "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=JSON";
        fake.responses[900] = {combined(25544, 54321)};
        auto sources = window.sourceUrls_;
        sources.append(group);
        saveSources(sources);
        update();
        assert(window.catalog_.find(25544) && window.catalog_.find(54321));

        // Satellite deletion removes its orbit but leaves the other member.
        remove(25544);
        assert(window.sourceUrls_.contains(group));
        assert(!window.sourceNorads({}, window.cachedSources_.value(group)).contains(25544));
        assert(window.sourceNorads({}, window.cachedSources_.value(group)).contains(54321));
        update();
        assert(!window.catalog_.find(25544));
        assert(!window.sourceNorads({}, window.cachedSources_.value(group)).contains(25544));
        assert(window.catalog_.find(54321));

        // Deleting the shared source removes its remaining dependent satellite.
        sources = window.sourceUrls_;
        sources.removeAll(group);
        saveSources(sources);
        assert(!window.catalog_.find(54321) && !window.cachedSources_.contains(group));
        assert(window.catalog_.find(61781) && window.catalog_.find(100465));

        // An explicit re-add of a source is an explicit re-add of its satellites.
        sources.append(group);
        saveSources(sources);
        update();
        assert(window.catalog_.find(25544) && window.catalog_.find(54321));
        // A successful feed replacement no longer supports its vanished member.
        fake.responses[900] = {orbit(54321)};
        update();
        assert(!window.catalog_.find(25544) && window.catalog_.find(54321));

        // A satellite supported by two URLs survives removal of just one.
        const QString duplicate = "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=JSON";
        fake.responses[901] = {orbit(61781)};
        sources = window.sourceUrls_;
        sources.append(duplicate);
        saveSources(sources);
        update();
        sources.removeAll(asrtu);
        saveSources(sources);
        assert(window.catalog_.find(61781));
        assert(window.catalog_.find(61781)->selectedHz == 436210000);
        sources.removeAll(duplicate);
        saveSources(sources);
        assert(!window.catalog_.find(61781));
        assert(!window.cachedSources_.contains(asrtu));
        assert(!window.cachedSources_.contains(duplicate));

        // Dedicated source disappears with its satellite, including local cache.
        remove(100465);
        assert(!window.sourceUrls_.contains(jamx) && !window.cachedSources_.contains(jamx));
        assert(!window.catalog_.find(100465));

        // Deleted and immediately re-added URL: stale in-flight response ignored.
        profiles = window.catalog_.entries();
        profiles.append({44444, "In flight", {}, 0});
        saveCatalog(profiles);
        fake.responses[44444] = {orbit(44444)};
        window.queueEphemerisLookup(44444);
        remove(44444);
        assert(!window.sourceUrls_.contains(window.celestrakSource(44444)));
        awaitCondition([&] { return !window.lookupReply_; });
        assert(!window.catalog_.find(44444));
        saveCatalog(profiles);
        window.queueEphemerisLookup(44444);
        remove(44444);
        saveCatalog(profiles);
        window.queueEphemerisLookup(44444);
        awaitCondition([&] { return !window.lookupReply_ && window.pendingLookups_.isEmpty(); });
        assert(window.catalog_.find(44444));
        assert(window.cachedSources_.contains(window.celestrakSource(44444)));

        // Partial refresh preserves a failed source's last valid orbit.
        fake.responses[44444] = {{}, QNetworkReply::TimeoutError};
        update();
        assert(window.catalog_.find(44444));
        assert(window.cachedSources_.contains(window.celestrakSource(44444)));
        fake.responses[44444] = {orbit(44444)};

        // Source editor is independently sized and deletion is immediately linked.
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            const int available = window.screen() ? window.screen()->availableGeometry().width() : 1024;
            assert(dialog->width() == qMax(400, qMin(430, available - 80)));
            capture(dialog, "sources.png");
            dialog->reject();
        });
        window.editSources();
        assert(window.saveSources({window.celestrakSource(44444)}, &error));
        window.installEphemerides(window.cachedSources_.values());
        assert(!window.catalog_.find(54321));
        assert(window.catalog_.entries().size() == 1);
        assert(window.sourceUrls_.size() == 1);

        // Deleting/re-adding a source during a bulk update invalidates the old
        // response and schedules a fresh update instead of losing the request.
        const auto onlySource = window.sourceUrls_;
        const int beforeRefresh = fake.requests.size();
        window.updateTle();
        saveSources({});
        saveSources(onlySource);
        window.updateTle();
        awaitCondition([&] { return fake.requests.size() >= beforeRefresh + 2 &&
            !window.downloading_ && window.catalog_.find(44444); });
        assert(window.catalog_.entries().size() == 1);

        // Failed disk write must not publish an unlinked catalog entry.
        const QString cache = QDir(qEnvironmentVariable("TINY_DOPPLER_CONFIG_DIR"))
                                  .filePath("ephemerides.json");
        const bool moved = QFile::rename(cache, cache + ".backup");
        const bool made = QDir().mkdir(cache);
        assert(moved && made);
        profiles = window.catalog_.entries();
        profiles.append({55555, "Disk failure", {}, 0});
        const bool saved = window.saveCatalogEntries(profiles, &error);
        assert(!saved && !window.catalog_.find(55555));
        assert(!window.sourceUrls_.contains(window.celestrakSource(55555)));
        persisted.sync();
        assert(persisted.value("tle_sources").toStringList() == window.sourceUrls_);
        const bool removedDirectory = QDir().rmdir(cache);
        const bool restored = QFile::rename(cache + ".backup", cache);
        assert(removedDirectory && restored);

        // Restart does not resurrect deleted satellite/source/cache records.
        QStringList legacySources = window.sourceUrls_;
        for (auto& source : legacySources)
            source.replace(QStringLiteral("FORMAT=JSON"), QStringLiteral("FORMAT=TLE"));
        persisted.setValue("tle_sources", legacySources);
        persisted.sync();
        const int warningsBeforeRestart = warnings.size();
        QPointer<QNetworkReply> inFlight;
        {
            SatelliteTrackerDialog reopened(0, 0, 0, {}, nullptr, false, factory, icon);
            assert(warnings.size() == warningsBeforeRestart);
            assert(reopened.catalog_.entries().size() == 1);
            assert(reopened.catalog_.find(44444));
            assert(reopened.sourceUrls_ == legacySources);
            assert(!reopened.cachedSources_.contains(group));
            inFlight = reopened.requestOrbit(QUrl(window.celestrakSource(44444)));
            assert(inFlight && inFlight->parent() == &reopened);
        }
        assert(inFlight.isNull());
        // Discovery permission for a deliberately re-added feed survives restart.
        sources = window.sourceUrls_;
        sources.append(group);
        fake.responses[900] = {combined(25544, 54321)};
        saveSources(sources);
        {
            SatelliteTrackerDialog reopened(0, 0, 0, {}, nullptr, false, factory, icon);
            assert(warnings.size() == warningsBeforeRestart);
            awaitCondition([&] { return !reopened.downloading_ && reopened.catalog_.find(25544); });
            assert(reopened.catalog_.find(54321));
        }
        capture(&window, "tracking.png");
        assert(!window.network_);
        const auto sourcesBeforeCustom = window.sourceUrls_;
        auto withCustomSource = sourcesBeforeCustom;
        withCustomSource.append(QStringLiteral("https://example.org/orbits.json"));
        assert(window.saveSources(withCustomSource, &error));
        assert(window.sourceUrls_ == withCustomSource);
        assert(window.saveSources(sourcesBeforeCustom, &error));
        assert(window.sourceUrls_ == sourcesBeforeCustom);
        closeWarnings.stop();
        // A corrected catalog must not reuse or rediscover the wrong orbit.
        window.catalog_.setEntries({{100470, "JAMX01", {435075000}, 435075000}});
        assert(window.catalog_.save(&error));
        const QString oldSource = window.celestrakSource(100465);
        persisted.setValue("tle_sources", QStringList{oldSource});
        persisted.sync();
        window.cachedSources_ = {{oldSource, orbit(100465)}};
        assert(window.saveEphemerisCache(&error));
        fake.responses[100470] = {orbit(100470)};
        {
            SatelliteTrackerDialog corrected(0, 0, 0, {}, nullptr, false, factory, icon);
            assert(!corrected.sourceUrls_.contains(oldSource));
            assert(!corrected.cachedSources_.contains(oldSource));
            assert(corrected.cachedSources_.isEmpty());
            awaitCondition([&] { return !corrected.downloading_ &&
                corrected.cachedSources_.contains(corrected.celestrakSource(100470)); });
            assert(!corrected.catalog_.find(100465));
            assert(corrected.catalog_.find(100470));
        }
        // Third-party sources are valid and must survive migration.
        const QString legacySource = "https://db.satnogs.org/api/tle/?norad_cat_id=100470";
        persisted.setValue("tle_sources", QStringList{legacySource});
        persisted.sync();
        window.cachedSources_ = {{legacySource, combined(100470, 12345)}};
        assert(window.saveEphemerisCache(&error));
        const int requestsBeforeMigration = fake.requests.size();
        {
            SatelliteTrackerDialog migrated(0, 0, 0, {}, nullptr, false, factory, icon);
            assert(migrated.sourceUrls_ == QStringList{legacySource});
            assert(migrated.cachedSources_.size() == 1);
            assert(migrated.catalog_.find(12345));
            assert(migrated.sourceNorads({}, migrated.cachedSources_.first()) ==
                   (QSet<int>{100470, 12345}));
            awaitCondition([&] { return fake.requests.size() > requestsBeforeMigration &&
                !migrated.downloading_; });
            for (int i = requestsBeforeMigration; i < fake.requests.size(); ++i)
                assert(fake.requests.at(i).host() == "db.satnogs.org" &&
                       fake.requests.at(i).scheme() == "https");
        }
        persisted.sync();
        assert(persisted.value("tle_sources").toStringList().contains(legacySource));
    }
};

static void changeFrequencyNote(CatalogDialog& dialog, int row, const QString& text,
                                bool accept = true, bool remove = false)
{
    auto* frequencies = dialog.findChild<QListWidget*>("frequencyList");
    QTimer::singleShot(0, &dialog, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        assert(menu);
        auto* action = menu->findChild<QAction*>(remove ? "removeFrequencyNote" : "editFrequencyNote");
        assert(action && action->isEnabled());
        menu->setActiveAction(action);
        if (!remove) {
            QTimer::singleShot(0, &dialog, [&] {
                auto* editor = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                assert(editor);
                editor->setTextValue(text);
                if (accept) editor->accept();
                else editor->reject();
            });
        }
        capture(menu, "frequency-menu.png");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(menu, &enter);
    });
    frequencies->customContextMenuRequested(frequencies->visualItemRect(frequencies->item(row)).center());
}

static void testCatalogUi()
{
    SatelliteCatalog catalog;
    catalog.setEntries({{61781, "ASRTU-1", {435400000, 436210000}, 435400000}});
    CatalogDialog dialog(catalog);
    dialog.show();
    QApplication::processEvents();
    auto* frequencies = dialog.findChild<QListWidget*>("frequencyList");
    assert(frequencies && !dialog.findChild<QLineEdit*>("frequencyNote"));
    for (const auto* button : dialog.findChildren<QPushButton*>())
        assert(button->text() != QStringLiteral("设为当前频率"));
    auto* input = dialog.findChild<QLineEdit*>("frequencyInput");
    auto* addFrequency = dialog.findChild<QPushButton*>("addFrequency");
    assert(input && input->text().isEmpty() && addFrequency && !addFrequency->isEnabled());
    for (const QString invalid : {QStringLiteral("0"), QStringLiteral("10001"),
                                  QStringLiteral("435.1234567"), QStringLiteral("abc")}) {
        input->setText(invalid);
        assert(!addFrequency->isEnabled());
    }
    input->setText("437.433001");
    assert(addFrequency->isEnabled());
    addFrequency->click();
    assert(frequencies->count() == 3 && input->text().isEmpty() && !addFrequency->isEnabled());
    assert(frequencies->item(2)->text().startsWith("437.433001 MHz"));
    dialog.findChild<QPushButton*>("removeFrequency")->click();
    assert(frequencies->count() == 2);
    changeFrequencyNote(dialog, 0, QStringLiteral("遥测"));
    changeFrequencyNote(dialog, 1, QStringLiteral("图像下传"));
    changeFrequencyNote(dialog, 1, QStringLiteral("Discard"), false);
    assert(frequencies->item(1)->text().contains(QStringLiteral("图像下传")));
    changeFrequencyNote(dialog, 0, {}, true, true);
    assert(!frequencies->item(0)->text().contains(QStringLiteral("遥测")));
    changeFrequencyNote(dialog, 0, QStringLiteral("遥测"));
    assert(frequencies->item(1)->text().contains(QStringLiteral("图像下传")));
    capture(&dialog, "catalog.png");
    auto* add = dialog.findChild<QPushButton*>("addSatellite");
    assert(add);
    QTimer::singleShot(0, &dialog, [&] {
        auto* entry = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        assert(entry && entry != &dialog);
        auto* name = entry->findChild<QLineEdit*>("satelliteName");
        auto* id = entry->findChild<QLineEdit*>("noradNumber");
        assert(name && id);
        name->setText("JAMX01");
        id->setText("invalid");
        auto* save = entry->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save);
        save->click();
        assert(QApplication::activeModalWidget() == entry);
        id->setText("A0465");
        capture(entry, "new-satellite.png");
        save->click();
    });
    add->click();
    assert(dialog.findChild<QListWidget*>("satelliteList")->count() == 2);
    dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    assert(catalog.find(100465) && catalog.find(100465)->name == "JAMX01");
    assert(catalog.find(61781)->selectedHz == 435400000);
    assert(catalog.find(61781)->frequencyNotes.value(436210000) == QStringLiteral("图像下传"));
    SatelliteCatalog reloaded;
    QString error;
    assert(reloaded.load(&error));
    assert(reloaded.find(61781)->frequencyNotes == catalog.find(61781)->frequencyNotes);
    CatalogDialog cancelled(catalog);
    cancelled.show();
    QApplication::processEvents();
    changeFrequencyNote(cancelled, 0, QStringLiteral("Discard"));
    cancelled.reject();
    assert(catalog.find(61781)->frequencyNotes.value(435400000) == QStringLiteral("遥测"));
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName("TinyDopplerWorkflowTest");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir directory;
    assert(directory.isValid());
    qputenv("TINY_DOPPLER_CONFIG_DIR", directory.path().toLocal8Bit());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path());
    testCatalogUi();
    // The tracker should start with a fresh, empty standalone catalog.
    const bool removed = QFile::remove(SatelliteCatalog::filePath());
    assert(removed);
    TrackerTestAccess::run();
    // Every reply is parented by the tracker or its network manager.  This
    // also covers destroying a tracker while a request is still in flight.
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    assert(FakeReply::liveCount == 0);
    TrackerTestAccess::checkLifetimes();
    return 0;
}
