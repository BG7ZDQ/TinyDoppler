#pragma once

#include "satellite_catalog.h"

#include <QDialog>
#include <QSet>

class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QGroupBox;

class CatalogDialog final : public QDialog {
    Q_OBJECT
public:
    explicit CatalogDialog(SatelliteCatalog& catalog, QWidget* parent = nullptr);

private:
    void refreshSatellites(int selectNorad = 0);
    void refreshDetails();
    void refreshFrequencies();
    void addSatellite();
    void removeSatellite();
    bool applyDetails(int row);
    void addFrequency();
    void removeFrequency();
    void selectFrequency();
    void accept() override;
    SatelliteProfile* currentProfile();

    SatelliteCatalog& catalog_;
    QList<SatelliteProfile> entries_;
    QSet<int> initialNorads_;
    QListWidget* satellites_ = nullptr;
    QLineEdit* norad_ = nullptr;
    QLineEdit* name_ = nullptr;
    QListWidget* frequencies_ = nullptr;
    QDoubleSpinBox* frequency_ = nullptr;
    QPushButton* removeSatelliteButton_ = nullptr;
    QGroupBox* details_ = nullptr;
    int activeRow_ = -1;
};
