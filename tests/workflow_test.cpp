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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
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

class FakeNetwork final : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QMap<int, Response> responses;
    QList<QUrl> requests;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request,
                                 QIODevice*) override
    {
        requests.append(request.url());
        const int id = QUrlQuery(request.url()).queryItemValue("CATNR").toInt();
        return new FakeReply(request, responses.value(id, {"No GP data found"}), this);
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
    assert(widget->grab().save(QDir(directory).filePath(QString::fromLatin1(name))));
}

struct TrackerTestAccess {
    static void run()
    {
        SatelliteTrackerDialog window(0, 0, 0, {});
        assert(window.catalog_.entries().isEmpty());
        assert(window.sourceUrls_.isEmpty());
        delete window.network_;
        auto* network = new FakeNetwork(&window);
        window.network_ = network;
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
        assert(window.catalog_.save(&error));
        window.cachedSources_.insert("previous-source", orbit(61781));
        assert(window.installEphemerides(window.cachedSources_.values()));
        window.refreshSatelliteChoices(61781);
        assert(window.frequencyMHz_->value() == 436.21);
        window.refreshSatelliteChoices(100465);
        assert(window.frequencyMHz_->value() == 435.5);
        window.refreshSatelliteChoices(61781);
        assert(window.frequencyMHz_->value() == 436.21);

        network->responses[100465] = {orbit(100465)};
        window.queueEphemerisLookup(100465);
        window.queueEphemerisLookup(100465);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(network->requests.size() == 1);
        assert(QUrlQuery(network->requests.first()).queryItemValue("FORMAT") == "JSON");
        assert(window.sourceUrls_.size() == 1);
        assert(window.satellites_.size() == 2);
        assert(window.cachedSources_.contains("previous-source"));
        assert(window.catalog_.find(100465)->name == "Custom JAMX name");
        assert(window.catalog_.find(61781)->selectedHz == 436210000);

        network->responses[123456] = {{}, QNetworkReply::TimeoutError};
        network->responses[22222] = {orbit(61781)};
        for (int id : {100469, 123456, 22222})
            window.queueEphemerisLookup(id);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(window.sourceUrls_.size() == 1);
        assert(window.catalog_.find(100469) && window.catalog_.find(123456));
        assert(window.satellites_.size() == 2);

        network->responses[44444] = {orbit(44444)};
        window.queueEphemerisLookup(44444);
        auto entries = window.catalog_.entries();
        entries.removeLast();
        window.catalog_.setEntries(entries);
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(!window.catalog_.find(44444));
        assert(window.sourceUrls_.size() == 1);

        window.downloading_ = true;
        const int beforeQueue = network->requests.size();
        network->responses[33333] = {orbit(33333)};
        window.queueEphemerisLookup(33333);
        assert(network->requests.size() == beforeQueue);
        window.downloading_ = false;
        window.startNextEphemerisLookup();
        awaitCondition([&] { return window.pendingLookups_.isEmpty(); });
        assert(window.sourceUrls_.size() == 2);
        assert(window.satellites_.size() == 3);

        // A partial refresh retains cached data from the failing source.
        window.sourceUrls_ = QStringList{
            "https://celestrak.org/NORAD/elements/gp.php?CATNR=100465&FORMAT=JSON",
            "https://celestrak.org/NORAD/elements/gp.php?CATNR=33333&FORMAT=JSON"};
        network->responses[33333] = {{}, QNetworkReply::TimeoutError};
        QTimer closeWarnings;
        QObject::connect(&closeWarnings, &QTimer::timeout, [] {
            for (QWidget* top : QApplication::topLevelWidgets())
                if (auto* warning = qobject_cast<QMessageBox*>(top))
                    warning->accept();
        });
        closeWarnings.start(10);
        window.updateTle();
        awaitCondition([&] { return !window.downloading_; });
        closeWarnings.stop();
        assert(window.satellites_.size() == 3);
        assert(window.cachedSources_.contains("previous-source"));

        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            assert(dialog);
            window.sourceUrls_.append("https://example.org/discovered.json");
            capture(dialog, "sources.png");
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
        });
        window.editSources();
        assert(window.sourceUrls_.contains("https://example.org/discovered.json"));
        capture(&window, "tracking.png");
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
    assert(QFile::remove(SatelliteCatalog::filePath()));
    TrackerTestAccess::run();
    return 0;
}
