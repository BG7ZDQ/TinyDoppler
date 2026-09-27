#include "catalog_dialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
class FrequencyRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        return {size.width() + 28, qMax(size.height(), 36)};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect row = option.rect.adjusted(2, 2, -2, -2);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(option.state & QStyle::State_Selected
                                        ? "#e6f1ff" : "#f3f8fe"));
            painter->drawRoundedRect(row, 5, 5);
        }
        if (index.data(Qt::UserRole).toBool()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#1766b2"));
            painter->drawEllipse(QPointF(row.left() + 13, row.center().y()), 3, 3);
        }
        // Reserve the same marker column for every row and every state.
        const QRect textRect = row.adjusted(28, 0, -8, 0);
        painter->setFont(option.font);
        painter->setPen(QColor("#17202a"));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
            option.fontMetrics.elidedText(index.data().toString(),
                                          Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};

class SatelliteRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 36));
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect row = option.rect.adjusted(2, 2, -2, -2);
        const bool selected = option.state & QStyle::State_Selected;
        if (selected || (option.state & QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? QColor(QStringLiteral("#e6f1ff"))
                                       : QColor(QStringLiteral("#f3f8fe")));
            painter->drawRoundedRect(row, 5, 5);
        }

        const QString name = index.data(Qt::UserRole).toString();
        const QString norad = index.data(Qt::UserRole + 1).toString();
        const QFontMetrics metrics(option.font);
        const int idWidth = qMax(66, metrics.horizontalAdvance(norad) + 12);
        const QRect nameRect = row.adjusted(10, 0, -idWidth - 12, 0);
        const QRect idRect(row.right() - idWidth - 8, row.top(),
                           idWidth, row.height());
        painter->setFont(option.font);
        painter->setPen(QColor(QStringLiteral("#17202a")));
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(name, Qt::ElideRight, nameRect.width()));
        QFont idFont = option.font;
        idFont.setWeight(QFont::DemiBold);
        painter->setFont(idFont);
        painter->setPen(QColor(selected ? QStringLiteral("#245f9d")
                                        : QStringLiteral("#617286")));
        painter->drawText(idRect, Qt::AlignRight | Qt::AlignVCenter, norad);
        painter->restore();
    }
};
}

CatalogDialog::CatalogDialog(SatelliteCatalog& catalog, QWidget* parent)
    : QDialog(parent), catalog_(catalog), entries_(catalog.entries())
{
    setWindowTitle(tr("卫星与频率管理"));
    for (const auto& entry : entries_)
        initialNorads_.insert(entry.norad);
    setMinimumSize(610, 430);
    resize(680, 490);
    setStyleSheet(QStringLiteral(
        "QDialog { background:#f5f8fc; }"
        "QWidget { color:#17202a; }"
        "QGroupBox { background:white; border:1px solid #dce5ef; "
        "border-radius:8px; margin:0; padding:0; font-weight:600; "
        "color:#17202a; }"
        "QLineEdit,QListWidget,QDoubleSpinBox { background:white; color:#17202a; "
        "border:1px solid #cbd5e1; border-radius:5px; padding:5px; }"
        "QListWidget { outline:0; }"
        "QListWidget::item { padding:5px 7px; }"
        "QListWidget::item:selected { background:#e6f1ff; color:#174f8c; }"
        "QListWidget::item:hover { background:#f3f8fe; }"
        "QPushButton { min-height:30px; border:1px solid #a9c9ef; "
        "border-radius:6px; background:#edf5ff; color:#145ca8; "
        "padding:0 11px; }"
        "QPushButton:hover { background:#e2f0ff; }"
        "QPushButton:disabled { color:#8495a8; background:#f5f7fa; "
        "border-color:#dce5ef; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(9);
    auto* columns = new QHBoxLayout;
    columns->setSpacing(10);
    auto* left = new QGroupBox(this);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(12, 12, 12, 12);
    auto* leftTitle = new QLabel(tr("卫星"), left);
    leftTitle->setStyleSheet(QStringLiteral("font-weight:600;"));
    leftLayout->addWidget(leftTitle);
    auto* listHeader = new QHBoxLayout;
    listHeader->setContentsMargins(10, 0, 10, 0);
    auto* nameHeader = new QLabel(tr("卫星名称"), left);
    auto* noradHeader = new QLabel(QStringLiteral("NORAD"), left);
    nameHeader->setStyleSheet(QStringLiteral("color:#617286; font-size:9pt;"));
    noradHeader->setStyleSheet(QStringLiteral("color:#617286; font-size:9pt;"));
    listHeader->addWidget(nameHeader, 1);
    listHeader->addWidget(noradHeader, 0, Qt::AlignRight);
    leftLayout->addLayout(listHeader);
    satellites_ = new QListWidget(left);
    satellites_->setObjectName(QStringLiteral("satelliteList"));
    satellites_->setItemDelegate(new SatelliteRowDelegate(satellites_));
    satellites_->setMouseTracking(true);
    satellites_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftLayout->addWidget(satellites_, 1);
    auto* satelliteActions = new QHBoxLayout;
    auto* addSatelliteButton = new QPushButton(tr("新增"), left);
    addSatelliteButton->setObjectName(QStringLiteral("addSatellite"));
    removeSatelliteButton_ = new QPushButton(tr("删除"), left);
    removeSatelliteButton_->setObjectName(QStringLiteral("removeSatellite"));
    satelliteActions->addWidget(addSatelliteButton);
    satelliteActions->addWidget(removeSatelliteButton_);
    leftLayout->addLayout(satelliteActions);
    columns->addWidget(left, 2);

    auto* right = new QGroupBox(this);
    details_ = right;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    auto* rightTitle = new QLabel(tr("详细设置"), right);
    rightTitle->setStyleSheet(QStringLiteral("font-weight:600;"));
    rightLayout->addWidget(rightTitle);
    auto* form = new QFormLayout;
    norad_ = new QLineEdit(right);
    name_ = new QLineEdit(right);
    name_->setMaxLength(120);
    form->addRow(tr("NORAD 编号"), norad_);
    form->addRow(tr("卫星名称"), name_);
    rightLayout->addLayout(form);
    rightLayout->addWidget(new QLabel(tr("下行频率"), right));
    frequencies_ = new QListWidget(right);
    frequencies_->setObjectName(QStringLiteral("frequencyList"));
    frequencies_->setItemDelegate(new FrequencyRowDelegate(frequencies_));
    frequencies_->setMouseTracking(true);
    frequencies_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    buttons->button(QDialogButtonBox::Save)->setStyleSheet(QStringLiteral(
        "QPushButton { background:#1766b2; border:1px solid #1766b2; "
        "color:white; border-radius:6px; font-weight:600; min-width:76px; "
        "min-height:30px; }"
        "QPushButton:hover { background:#0f579e; }"));
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
        auto* item = new QListWidgetItem(profile.name, satellites_);
        item->setData(Qt::UserRole, profile.name);
        item->setData(Qt::UserRole + 1, QString::number(profile.norad));
        item->setToolTip(QStringLiteral("%1 · NORAD %2")
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
    details_->setEnabled(profile != nullptr);
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
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', 6), frequencies_);
        item->setData(Qt::UserRole, hz == profile->selectedHz);
        item->setToolTip(item->text());
    }
    if (frequencies_->count() > 0) {
        const int selected = profile->frequenciesHz.indexOf(profile->selectedHz);
        frequencies_->setCurrentRow(selected >= 0 ? selected : 0);
    }
}

void CatalogDialog::addSatellite()
{
    if (!applyDetails(activeRow_))
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("新增卫星"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(10);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("satelliteName"));
    name->setMaxLength(120);
    name->setMinimumWidth(240);
    auto* id = new QLineEdit(&dialog);
    id->setObjectName(QStringLiteral("noradNumber"));
    form->addRow(tr("卫星名称"), name);
    form->addRow(tr("NORAD 编号"), id);
    layout->addLayout(form);
    auto* error = new QLabel(&dialog);
    error->setStyleSheet(QStringLiteral("color:#b53636;"));
    error->setWordWrap(true);
    error->hide();
    connect(name, &QLineEdit::textChanged, error, &QWidget::hide);
    connect(id, &QLineEdit::textChanged, error, &QWidget::hide);
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("添加"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    int norad = 0;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QString message;
        if (name->text().trimmed().isEmpty()) {
            message = tr("请填写不超过 120 个字符的卫星名称。");
            name->setFocus();
        } else if (!SatelliteCatalog::parseNorad(id->text(), &norad)) {
            message = tr("请输入十进制编号或标准的五字符 Alpha-5 编号。");
            id->setFocus();
        } else {
            for (const auto& entry : entries_) {
                if (entry.norad == norad) {
                    message = tr("该 NORAD 编号已在列表中。");
                    id->setFocus();
                    break;
                }
            }
        }
        if (!message.isEmpty()) {
            error->setText(message);
            error->show();
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    name->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;
    entries_.append(SatelliteProfile{norad, name->text().trimmed(), {}, 0});
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
    auto* item = satellites_->item(row);
    item->setText(profile->name);
    item->setData(Qt::UserRole, profile->name);
    item->setData(Qt::UserRole + 1, QString::number(profile->norad));
    item->setToolTip(QStringLiteral("%1 · NORAD %2")
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
    // A download can finish while this modal editor is open. Keep newly
    // discovered satellites without undoing explicit edits or deletions.
    QSet<int> editedNorads;
    for (const auto& entry : entries_)
        editedNorads.insert(entry.norad);
    for (const auto& entry : previous) {
        if (!initialNorads_.contains(entry.norad) &&
            !editedNorads.contains(entry.norad))
            entries_.append(entry);
    }
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
