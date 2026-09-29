#include "satellite_tracker_dialog.h"
#include "catalog_dialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <cstring>
#include <functional>

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
    FakeReply(const QNetworkRequest& request, Response response, QObject* parent)
        : QNetworkReply(parent), body_(std::move(response.body))
    {
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

class FakeNetwork final {
public:
    QMap<int, Response> responses;
    QList<QUrl> requests;
    QNetworkReply* get(const QNetworkRequest& request, QObject* parent)
    {
        requests.append(request.url());
        const int id = QUrlQuery(request.url()).queryItemValue("CATNR").toInt();
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
    static void run()
    {
        FakeNetwork fake;
        auto* network = &fake;
        const auto requestFactory = [&fake](const QNetworkRequest& request, QObject* parent) {
            return fake.get(request, parent);
        };
        // The workflow tests own neither networking nor raster conversion.
        // A large PNG starts the system Qt image-conversion thread pool; that
        // uninstrumented library reports TSan races even in a Qt-only probe.
        // Keep the real icon for optional screenshot QA, not sanitizer runs.
        const QIcon icon = qEnvironmentVariableIsEmpty("TINY_DOPPLER_QA_DIR")
            ? QIcon() : QIcon(QStringLiteral(":/tiny/icon.png"));
        SatelliteTrackerDialog window(0, 0, 0, {}, nullptr, false, requestFactory, icon);
        assert(window.catalog_.entries().isEmpty());
        assert(window.sourceUrls_.isEmpty());
        assert(!window.network_);
        window.catalog_.setEntries({
            {61781, "Custom ASRTU name", {435400000, 436210000}, 436210000},
            {100465, "Custom JAMX name", {435500000}, 435500000},
            {100469, "Missing orbit", {}, 0},
            {123456, "Offline", {}, 0},
            {22222, "Wrong response", {}, 0},
            {33333, "Queued", {}, 0},
            {44444, "Removed during lookup", {}, 0}
        });
        QString error;
        const bool catalogSaved = window.catalog_.save(&error);
        assert(catalogSaved);
        window.cachedSources_.insert("legacy:0", orbit(61781));
        const bool installed = window.installEphemerides(window.cachedSources_.values());
        assert(installed);
        window.refreshSatelliteChoices(61781);
        assert(window.frequencyMHz_->value() == 436.21);
        window.refreshSatelliteChoices(100465);
        assert(window.frequencyMHz_->value() == 435.5);
        window.refreshSatelliteChoices(61781);
        assert(window.frequencyMHz_->value() == 436.21);

        network->responses[100465] = {orbit(100465)};
        window.queueEphemerisLookup(100465);
        window.queueEphemerisLookup(100465);
        assert(window.sourceUrls_.size() == 1);
        assert(window.cachedSources_.size() == 1); // Reply has not arrived.
        QSettings persisted(QSettings::IniFormat, QSettings::UserScope,
                            "TinyDoppler", "StandaloneTracker");
        assert(persisted.value("tle_sources").toStringList() == window.sourceUrls_);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(network->requests.size() == 1);
        assert(QUrlQuery(network->requests.first()).queryItemValue("FORMAT") == "JSON");
        assert(window.sourceUrls_.size() == 1);
        assert(window.satellites_.size() == 2);
        assert(window.cachedSources_.contains("legacy:0"));
        assert(window.catalog_.find(100465)->name == "Custom JAMX name");
        assert(window.catalog_.find(61781)->selectedHz == 436210000);

        network->responses[123456] = {{}, QNetworkReply::TimeoutError};
        network->responses[22222] = {orbit(61781)};
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
        for (int id : {100469, 123456, 22222})
            window.queueEphemerisLookup(id);
        assert(window.sourceUrls_.size() == 4); // Includes not-yet-started requests.
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(window.sourceUrls_.size() == 4);
        assert(warnings.size() == 3);
        for (int index = 0; index < 3; ++index) {
            const int id = QList<int>{100469, 123456, 22222}.at(index);
            assert(warnings.at(index).contains(QString("CATNR=%1").arg(id)));
            assert(!window.cachedSources_.contains(window.celestrakSource(id)));
        }
        assert(warnings.at(1).contains("Test network failure"));
        persisted.sync();
        assert(persisted.value("tle_sources").toStringList() == window.sourceUrls_);
        assert(window.catalog_.find(100469) && window.catalog_.find(123456));
        assert(window.satellites_.size() == 2);

        network->responses[44444] = {orbit(44444)};
        window.queueEphemerisLookup(44444);
        auto entries = window.catalog_.entries();
        entries.removeLast();
        window.catalog_.setEntries(entries);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(!window.catalog_.find(44444));
        assert(window.sourceUrls_.size() == 5);
        assert(warnings.size() == 3); // No warning for a satellite removed in flight.

        window.downloading_ = true;
        const int beforeQueue = network->requests.size();
        network->responses[33333] = {orbit(33333)};
        window.queueEphemerisLookup(33333);
        assert(network->requests.size() == beforeQueue);
        window.downloading_ = false;
        window.startNextEphemerisLookup();
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(window.sourceUrls_.size() == 6);
        assert(window.satellites_.size() == 3);

        // A partial refresh retains cached data from the failing source.
        window.sourceUrls_ = QStringList{
            "https://celestrak.org/NORAD/elements/gp.php?CATNR=100465&FORMAT=JSON",
            "https://celestrak.org/NORAD/elements/gp.php?CATNR=33333&FORMAT=JSON"};
        network->responses[33333] = {{}, QNetworkReply::TimeoutError};
        window.updateTle();
        awaitCondition([&] { return !window.downloading_; });
        closeWarnings.stop();
        assert(window.satellites_.size() == 3);
        assert(window.cachedSources_.contains("legacy:0"));

        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            window.sourceUrls_.append("https://example.org/discovered.json");
            capture(dialog, "sources.png");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        window.editSources();
        assert(window.sourceUrls_.contains("https://example.org/discovered.json"));
        // Removed source caches (including legacy, unattributed caches) must
        // not resurrect an ISS that is no longer in the user's catalog.
        const QString issSource =
            "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=JSON";
        window.cachedSources_.insert(issSource, orbit(25544));
        window.cachedSources_.insert("legacy:iss", orbit(25544));
        window.installEphemerides(window.cachedSources_.values());
        assert(!window.catalog_.find(25544));
        window.pruneEphemerisCache();
        assert(!window.cachedSources_.contains(issSource));

        // A genuinely new satellite from a fresh download is still discovered.
        window.installEphemerides(window.cachedSources_.values(), {orbit(25544)});
        assert(window.catalog_.find(25544));
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            auto* list = dialog->findChild<QListWidget*>("satelliteList");
            for (int row = 0; row < list->count(); ++row) {
                if (list->item(row)->data(Qt::UserRole + 1).toInt() == 25544)
                    list->setCurrentRow(row);
            }
            QTimer::singleShot(0, dialog, [] {
                auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                assert(question);
                question->button(QMessageBox::Yes)->click();
            });
            dialog->findChild<QPushButton*>("removeSatellite")->click();
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        window.editCatalog();
        assert(!window.catalog_.find(25544));
        assert(window.dismissedNorads_.contains(25544));
        window.installEphemerides(window.cachedSources_.values(), {orbit(25544)});
        assert(!window.catalog_.find(25544));
        window.saveEphemerisCache();
        QPointer<QNetworkReply> inFlight;
        {
            SatelliteTrackerDialog reopened(0, 0, 0, {}, nullptr, false, requestFactory, icon);
            assert(!reopened.catalog_.find(25544));
            assert(reopened.dismissedNorads_.contains(25544));
            // Closing a window must also destroy its pending replies/timers.
            inFlight = reopened.requestOrbit(QUrl(issSource));
            assert(inFlight && inFlight->parent() == &reopened);
            assert(!reopened.network_);
        }
        assert(inFlight.isNull());
        // Re-adding a deleted satellite must query even with a legacy orbit
        // already loaded. Otherwise it never acquires a refreshable source.
        assert(!window.sourceUrls_.contains(issSource));
        const int requestsBeforeReadd = network->requests.size();
        network->responses[25544] = {orbit(25544)};
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            QTimer::singleShot(0, dialog, [] {
                auto* entry = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                assert(entry);
                entry->findChild<QLineEdit*>("satelliteName")->setText("ISS");
                entry->findChild<QLineEdit*>("noradNumber")->setText("25544");
                entry->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            });
            dialog->findChild<QPushButton*>("addSatellite")->click();
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        window.editCatalog();
        assert(window.sourceUrls_.contains(issSource)); // Before the response.
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(network->requests.size() == requestsBeforeReadd + 1);
        assert(network->requests.last() == QUrl(issSource));
        assert(window.catalog_.find(25544));
        assert(!window.dismissedNorads_.contains(25544));
        assert(window.sourceUrls_.count(issSource) == 1);
        assert(window.cachedSources_.value(issSource) == orbit(25544));
        // Saving an unchanged catalog neither queries again nor duplicates URLs.
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        window.editCatalog();
        assert(network->requests.size() == requestsBeforeReadd + 1);
        assert(window.sourceUrls_.count(issSource) == 1);
        capture(&window, "tracking.png");
        assert(!window.network_); // No real network manager, including during refresh.
    }
};

static void testCatalogUi()
{
    SatelliteCatalog catalog;
    catalog.setEntries({{61781, "ASRTU-1", {435400000, 436210000}, 435400000}});
    CatalogDialog dialog(catalog);
    dialog.show();
    QApplication::processEvents();
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
    return 0;
}
