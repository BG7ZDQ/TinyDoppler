#include "catalog_dialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CatalogDialog::CatalogDialog(SatelliteCatalog& catalog, QWidget* parent)
    : QDialog(parent), catalog_(catalog), entries_(catalog.entries())
{
    setWindowTitle(tr("管理卫星与频率"));
    setMinimumSize(590, 420);
    resize(650, 470);
    setStyleSheet(QStringLiteral(
        "QDialog { background:#f5f8fc; }"
        "QWidget { color:#17202a; }"
        "QGroupBox { background:white; border:1px solid #dce5ef; "
        "border-radius:7px; margin-top:10px; padding-top:9px; font-weight:600; "
        "color:#17202a; }"
        "QLineEdit,QListWidget,QDoubleSpinBox { background:white; color:#17202a; "
        "border:1px solid #cbd5e1; border-radius:5px; padding:4px; }"
        "QPushButton { min-height:29px; border:1px solid #b8c9de; "
        "border-radius:5px; background:#f7fbff; color:#17202a; "
        "padding:0 10px; }"
        "QPushButton:hover { background:#e5f1ff; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(9);
    auto* columns = new QHBoxLayout;
    columns->setSpacing(10);
    auto* left = new QGroupBox(tr("卫星"), this);
    auto* leftLayout = new QVBoxLayout(left);
    satellites_ = new QListWidget(left);
    leftLayout->addWidget(satellites_, 1);
    auto* satelliteActions = new QHBoxLayout;
    auto* addSatelliteButton = new QPushButton(tr("新增"), left);
    removeSatelliteButton_ = new QPushButton(tr("删除"), left);
    satelliteActions->addWidget(addSatelliteButton);
    satelliteActions->addWidget(removeSatelliteButton_);
    leftLayout->addLayout(satelliteActions);
    columns->addWidget(left, 2);

    auto* right = new QGroupBox(tr("详细设置"), this);
    auto* rightLayout = new QVBoxLayout(right);
    auto* form = new QFormLayout;
    norad_ = new QLineEdit(right);
    name_ = new QLineEdit(right);
    name_->setMaxLength(120);
    form->addRow(tr("NORAD 编号"), norad_);
    form->addRow(tr("卫星名称"), name_);
    rightLayout->addLayout(form);
    rightLayout->addWidget(new QLabel(tr("下行频率"), right));
    frequencies_ = new QListWidget(right);
    rightLayout->addWidget(frequencies_, 1);
    auto* frequencyActions = new QHBoxLayout;
    frequency_ = new QDoubleSpinBox(right);
    frequency_->setRange(1.0, 10000.0);
    frequency_->setDecimals(6);
    frequency_->setSuffix(QStringLiteral(" MHz"));
    frequency_->setValue(435.4);
    auto* addFrequencyButton = new QPushButton(tr("添加"), right);
    auto* removeFrequencyButton = new QPushButton(tr("删除"), right);
    frequencyActions->addWidget(frequency_, 1);
    frequencyActions->addWidget(addFrequencyButton);
    frequencyActions->addWidget(removeFrequencyButton);
    rightLayout->addLayout(frequencyActions);
    auto* selectButton = new QPushButton(tr("设为当前频率"), right);
    rightLayout->addWidget(selectButton, 0, Qt::AlignRight);
    columns->addWidget(right, 3);
    outer->addLayout(columns, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    outer->addWidget(buttons);

    connect(satellites_, &QListWidget::currentRowChanged, this,
            [this](int newRow) {
                if (activeRow_ >= 0 && !applyDetails(activeRow_)) {
                    satellites_->blockSignals(true);
                    satellites_->setCurrentRow(activeRow_);
                    satellites_->blockSignals(false);
                    return;
                }
                activeRow_ = newRow;
                refreshDetails();
            });
    connect(addSatelliteButton, &QPushButton::clicked, this,
            [this] { addSatellite(); });
    connect(removeSatelliteButton_, &QPushButton::clicked, this,
            [this] { removeSatellite(); });
    connect(addFrequencyButton, &QPushButton::clicked, this,
            [this] { addFrequency(); });
    connect(removeFrequencyButton, &QPushButton::clicked, this,
            [this] { removeFrequency(); });
    connect(selectButton, &QPushButton::clicked, this,
            [this] { selectFrequency(); });
    connect(frequencies_, &QListWidget::itemDoubleClicked, this,
            [this] { selectFrequency(); });
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this] { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this,
            &QDialog::reject);
    refreshSatellites();
}

SatelliteProfile* CatalogDialog::currentProfile()
{
    const int row = satellites_->currentRow();
    return row >= 0 && row < entries_.size() ? &entries_[row] : nullptr;
}

void CatalogDialog::refreshSatellites(int selectNorad)
{
    satellites_->blockSignals(true);
    satellites_->clear();
    int selectedRow = entries_.isEmpty() ? -1 : 0;
    for (int row = 0; row < entries_.size(); ++row) {
        const SatelliteProfile& profile = entries_.at(row);
        satellites_->addItem(QStringLiteral("%1  ·  %2")
                                 .arg(profile.name).arg(profile.norad));
        if (profile.norad == selectNorad)
            selectedRow = row;
    }
    satellites_->setCurrentRow(selectedRow);
    satellites_->blockSignals(false);
    activeRow_ = selectedRow;
    refreshDetails();
}

void CatalogDialog::refreshDetails()
{
    SatelliteProfile* profile = currentProfile();
    norad_->setEnabled(profile != nullptr);
    name_->setEnabled(profile != nullptr);
    frequencies_->setEnabled(profile != nullptr);
    removeSatelliteButton_->setEnabled(profile != nullptr);
    norad_->setText(profile ? QString::number(profile->norad) : QString());
    name_->setText(profile ? profile->name : QString());
    refreshFrequencies();
}

void CatalogDialog::refreshFrequencies()
{
    frequencies_->clear();
    const SatelliteProfile* profile = currentProfile();
    if (!profile)
        return;
    for (qint64 hz : profile->frequenciesHz) {
        frequencies_->addItem(QStringLiteral("%1  %2 MHz")
                                  .arg(hz == profile->selectedHz
                                           ? QStringLiteral("●")
                                           : QStringLiteral("  "))
                                  .arg(hz / 1e6, 0, 'f', 6));
    }
    if (frequencies_->count() > 0)
        frequencies_->setCurrentRow(0);
}

void CatalogDialog::addSatellite()
{
    if (!applyDetails(activeRow_))
        return;
    bool accepted = false;
    const QString input = QInputDialog::getText(
        this, tr("新增卫星"), tr("NORAD 编号"), QLineEdit::Normal, {}, &accepted);
    if (!accepted)
        return;
    int norad = 0;
    if (!SatelliteCatalog::parseNorad(input, &norad)) {
        QMessageBox::warning(this, tr("编号无效"),
                             tr("请输入十进制编号或标准的五字符 Alpha-5 编号。"));
        return;
    }
    for (const SatelliteProfile& entry : entries_) {
        if (entry.norad == norad) {
            QMessageBox::warning(this, tr("卫星已存在"),
                                 tr("该 NORAD 编号已在列表中。"));
            return;
        }
    }
    const QString name = QInputDialog::getText(
        this, tr("新增卫星"), tr("卫星名称"), QLineEdit::Normal, {}, &accepted)
                             .trimmed();
    if (!accepted)
        return;
    if (name.isEmpty() || name.size() > 120) {
        QMessageBox::warning(this, tr("名称不能为空"),
                             tr("请填写不超过 120 个字符的卫星名称。"));
        return;
    }
    entries_.append(SatelliteProfile{norad, name, {}, 0});
    refreshSatellites(norad);
}

void CatalogDialog::removeSatellite()
{
    if (!applyDetails(activeRow_))
        return;
    const int row = satellites_->currentRow();
    if (row < 0 || row >= entries_.size())
        return;
    if (QMessageBox::question(this, tr("删除卫星"),
                              tr("确定删除“%1”及其频率吗？")
                                  .arg(entries_.at(row).name)) != QMessageBox::Yes)
        return;
    entries_.removeAt(row);
    refreshSatellites();
}

bool CatalogDialog::applyDetails(int row)
{
    if (row < 0 || row >= entries_.size())
        return true;
    SatelliteProfile* profile = &entries_[row];
    int norad = 0;
    if (!SatelliteCatalog::parseNorad(norad_->text(), &norad) ||
        name_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("输入无效"),
                             tr("请填写有效的 NORAD 编号和卫星名称。"));
        return false;
    }
    for (const SatelliteProfile& entry : entries_) {
        if (&entry != profile && entry.norad == norad) {
            QMessageBox::warning(this, tr("编号重复"),
                                 tr("这个 NORAD 编号已被另一颗卫星使用。"));
            return false;
        }
    }
    profile->norad = norad;
    profile->name = name_->text().trimmed();
    satellites_->item(row)->setText(QStringLiteral("%1  ·  %2")
                                        .arg(profile->name).arg(profile->norad));
    return true;
}

void CatalogDialog::addFrequency()
{
    SatelliteProfile* profile = currentProfile();
    if (!profile)
        return;
    const qint64 hz = qRound64(frequency_->value() * 1e6);
    if (profile->frequenciesHz.contains(hz)) {
        QMessageBox::information(this, tr("频率已存在"),
                                 tr("该频率已在列表中。"));
        return;
    }
    profile->frequenciesHz.append(hz);
    if (profile->selectedHz == 0)
        profile->selectedHz = hz;
    refreshFrequencies();
    frequencies_->setCurrentRow(profile->frequenciesHz.size() - 1);
}

void CatalogDialog::removeFrequency()
{
    SatelliteProfile* profile = currentProfile();
    const int row = frequencies_->currentRow();
    if (!profile || row < 0 || row >= profile->frequenciesHz.size())
        return;
    const qint64 removed = profile->frequenciesHz.takeAt(row);
    if (profile->selectedHz == removed)
        profile->selectedHz = profile->frequenciesHz.isEmpty()
                                  ? 0 : profile->frequenciesHz.first();
    refreshFrequencies();
}

void CatalogDialog::selectFrequency()
{
    SatelliteProfile* profile = currentProfile();
    const int row = frequencies_->currentRow();
    if (!profile || row < 0 || row >= profile->frequenciesHz.size())
        return;
    profile->selectedHz = profile->frequenciesHz.at(row);
    refreshFrequencies();
    frequencies_->setCurrentRow(row);
}

void CatalogDialog::accept()
{
    if (!applyDetails(satellites_->currentRow()))
        return;
    const QList<SatelliteProfile> previous = catalog_.entries();
    catalog_.setEntries(entries_);
    QString error;
    if (!catalog_.save(&error)) {
        catalog_.setEntries(previous);
        QMessageBox::critical(this, tr("无法保存"),
                              tr("卫星设置未保存：%1").arg(error));
        return;
    }
    QDialog::accept();
}
