#include "gui/filetyperefdialog.h"

#include "core/configrepository.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace rfv {
namespace {

QString joinVersions(const QStringList& versions)
{
    QStringList sorted = versions;
    std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return sorted.join(QStringLiteral("、"));
}

QString joinSections(const QStringList& sections)
{
    return sections.join(QStringLiteral("、"));
}

/// 中登：把“开放式基金-交易类确认”拆成“开放式基金”和“交易类确认”。
void splitOfdDescribe(const InterfaceDefinition& def, QString* scope, QString* name)
{
    const int dash = def.describe.indexOf(QLatin1Char('-'));
    if (dash > 0) {
        *scope = def.describe.left(dash).trimmed();
        *name = def.describe.mid(dash + 1).trimmed();
    } else {
        *scope = def.protocol;
        *name = def.describe;
    }
}

/// 理财登：filedescribe 形如“008-04-交易确认”“信息披露-ID1-直接发送”。
void splitFixedDescribe(const QString& describe, QString* fileNo, QString* code,
                        QString* name)
{
    const QStringList parts = describe.split(QLatin1Char('-'));
    if (parts.size() >= 3) {
        bool firstIsNumber = false;
        parts.at(0).trimmed().toInt(&firstIsNumber);
        if (firstIsNumber) {
            *fileNo = parts.at(0).trimmed();
            *code = parts.at(1).trimmed();
            *name = parts.mid(2).join(QLatin1Char('-')).trimmed();
        } else {
            fileNo->clear();
            *code = parts.at(1).trimmed();
            *name = parts.at(0).trimmed() + QLatin1Char('-')
                    + parts.mid(2).join(QLatin1Char('-')).trimmed();
        }
    } else if (parts.size() == 2) {
        fileNo->clear();
        *code = parts.at(0).trimmed();
        *name = parts.at(1).trimmed();
    } else {
        fileNo->clear();
        *code = QStringLiteral("-");
        *name = describe;
    }

    if (code->isEmpty())
        *code = QStringLiteral("-");
}

QList<FileTypeReferenceRow> buildOfdRows(const ConfigRepository& repository)
{
    QMap<QString, FileTypeReferenceRow> grouped;
    for (const InterfaceDefinition& def : repository.ofdDefinitions()) {
        if (!def.usable || def.fileType.isEmpty())
            continue;

        QString scope;
        QString name;
        splitOfdDescribe(def, &scope, &name);

        const QString key = def.fileType + QLatin1Char('|') + scope
                + QLatin1Char('|') + name;
        FileTypeReferenceRow& row = grouped[key];
        row.code = def.fileType;
        row.scope = scope;
        row.name = name;
        if (!def.versionLabel.isEmpty() && !row.versions.contains(def.versionLabel))
            row.versions.append(def.versionLabel);
        if (!def.section.isEmpty() && !row.sections.contains(def.section))
            row.sections.append(def.section);
    }
    return grouped.values();
}

QList<FileTypeReferenceRow> buildWealthRows(const ConfigRepository& repository)
{
    QMap<QString, FileTypeReferenceRow> grouped;
    for (const InterfaceDefinition& def : repository.fixedDefinitions()) {
        if (!def.usable)
            continue;
        if (!def.configFile.startsWith(QLatin1String("FIXED_WDEP"))
            && !def.configFile.startsWith(QLatin1String("FIXED_WPID"))) {
            continue;
        }

        QString fileNo;
        QString code;
        QString name;
        splitFixedDescribe(def.describe, &fileNo, &code, &name);

        const QString key = fileNo + QLatin1Char('|') + code
                + QLatin1Char('|') + name;
        FileTypeReferenceRow& row = grouped[key];
        row.code = code;
        row.fileNo = fileNo;
        row.name = name;
        if (!def.versionLabel.isEmpty() && !row.versions.contains(def.versionLabel))
            row.versions.append(def.versionLabel);
        if (!def.section.isEmpty() && !row.sections.contains(def.section))
            row.sections.append(def.section);
    }
    return grouped.values();
}

int branchSortRank(const QString& branch)
{
    if (branch == QStringLiteral("开放式基金"))
        return 0;
    if (branch == QStringLiteral("商业养老金"))
        return 1;
    if (branch == QStringLiteral("集中备份"))
        return 2;
    if (branch.startsWith(QStringLiteral("平安理财子定制")))
        return 3;
    if (branch == QStringLiteral("银行理财(恒生接口)"))
        return 4;
    return 5;
}

QTableWidgetItem* makeCodeItem(const FileTypeReferenceRow& row)
{
    auto* item = new QTableWidgetItem(row.code);
    item->setTextAlignment(Qt::AlignCenter);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    if (!row.sections.isEmpty())
        item->setToolTip(QStringLiteral("配置段：%1").arg(joinSections(row.sections)));
    return item;
}

QTableWidgetItem* makeNameItem(const FileTypeReferenceRow& row)
{
    auto* item = new QTableWidgetItem(row.name);
    if (!row.sections.isEmpty())
        item->setToolTip(QStringLiteral("配置段：%1").arg(joinSections(row.sections)));
    return item;
}

} // namespace

FileTypeReferenceDialog::FileTypeReferenceDialog(const ConfigRepository& repository,
                                                 FileTypeReferenceKind kind,
                                                 QWidget* parent)
    : QDialog(parent)
{
    const bool ofd = kind == FileTypeReferenceKind::Ofd;
    setWindowTitle(ofd ? QStringLiteral("中登文件类型参考")
                       : QStringLiteral("理财登文件类型参考"));
    resize(900, 620);

    auto* layout = new QVBoxLayout(this);

    if (ofd) {
        auto* branchLayout = new QHBoxLayout();
        branchLayout->addWidget(new QLabel(QStringLiteral("适用分支："), this));

        m_branchTabs = new QTabBar(this);
        m_branchTabs->setExpanding(false);
        m_branchTabs->setUsesScrollButtons(true);
        m_branchTabs->setElideMode(Qt::ElideNone);
        m_branchTabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        branchLayout->addWidget(m_branchTabs, 1);
        layout->addLayout(branchLayout);

        auto* versionLayout = new QHBoxLayout();
        versionLayout->addWidget(new QLabel(QStringLiteral("版本："), this));

        m_versionTabs = new QTabBar(this);
        m_versionTabs->setExpanding(false);
        m_versionTabs->setUsesScrollButtons(true);
        m_versionTabs->setElideMode(Qt::ElideNone);
        m_versionTabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        versionLayout->addWidget(m_versionTabs, 1);

        m_countLabel = new QLabel(this);
        m_countLabel->setStyleSheet(QStringLiteral("color:#6b7280;"));
        versionLayout->addWidget(m_countLabel);
        layout->addLayout(versionLayout);
    } else {
        auto* filterLayout = new QHBoxLayout();
        m_filterEdit = new QLineEdit(this);
        m_filterEdit->setPlaceholderText(
            QStringLiteral("筛选文件类型或关键字，例如 04、C1、交易确认"));
        m_filterEdit->setClearButtonEnabled(true);
        filterLayout->addWidget(m_filterEdit, 1);

        m_countLabel = new QLabel(this);
        m_countLabel->setStyleSheet(QStringLiteral("color:#6b7280;"));
        filterLayout->addWidget(m_countLabel);
        layout->addLayout(filterLayout);
    }

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ofd ? 3 : 4);
    m_table->setHorizontalHeaderLabels(
        ofd ? QStringList() << QStringLiteral("文件类型")
                            << QStringLiteral("文件或交易类型")
                            << QStringLiteral("配置段")
            : QStringList() << QStringLiteral("业务类型")
                            << QStringLiteral("文件或交易类型")
                            << QStringLiteral("文件名编号")
                            << QStringLiteral("版本"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);

    if (ofd)
        setupOfdTabs(repository);
    else
        fillWealthTable(repository);

    QHeaderView* header = m_table->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    if (ofd) {
        header->setSectionResizeMode(2, QHeaderView::Interactive);
        m_table->setColumnWidth(2, 220);
    } else {
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    }

    layout->addWidget(m_table, 1);

    if (!ofd) {
        connect(m_filterEdit, &QLineEdit::textChanged, this,
                [this](const QString& text) { applyFilter(text); });
        applyFilter(QString());
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void FileTypeReferenceDialog::setupOfdTabs(const ConfigRepository& repository)
{
    m_ofdRows = buildOfdRows(repository);

    QMap<QString, QStringList> branchVersions;
    for (const FileTypeReferenceRow& row : m_ofdRows) {
        QStringList& versions = branchVersions[row.scope];
        for (const QString& version : row.versions) {
            if (!versions.contains(version))
                versions.append(version);
        }
    }

    QStringList branches = branchVersions.keys();
    std::sort(branches.begin(), branches.end(),
              [](const QString& a, const QString& b) {
        const int rankA = branchSortRank(a);
        const int rankB = branchSortRank(b);
        if (rankA != rankB)
            return rankA < rankB;
        return a.localeAwareCompare(b) < 0;
    });

    for (const QString& branch : branches) {
        QStringList versions = branchVersions.value(branch);
        std::sort(versions.begin(), versions.end(),
                  [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        m_branchVersions.insert(branch, versions);

        const int index = m_branchTabs->addTab(branch);
        m_branchTabs->setTabData(index, branch);
        m_branchTabs->setTabToolTip(index, branch);
    }

    connect(m_branchTabs, &QTabBar::currentChanged, this,
            [this](int index) { populateVersionTabs(index); });
    connect(m_versionTabs, &QTabBar::currentChanged, this,
            [this](int) { populateOfdTable(); });

    if (m_branchTabs->count() > 0) {
        {
            const QSignalBlocker blocker(m_branchTabs);
            m_branchTabs->setCurrentIndex(0);
        }
        populateVersionTabs(0);
    } else {
        m_table->setRowCount(0);
        if (m_countLabel)
            m_countLabel->setText(QStringLiteral("共 0 项"));
    }
}

void FileTypeReferenceDialog::populateVersionTabs(int branchIndex)
{
    if (!m_branchTabs || !m_versionTabs)
        return;

    const QString branch = branchIndex >= 0
            ? m_branchTabs->tabData(branchIndex).toString() : QString();
    const QStringList versions = m_branchVersions.value(branch);

    {
        const QSignalBlocker blocker(m_versionTabs);
        while (m_versionTabs->count() > 0)
            m_versionTabs->removeTab(0);
        for (const QString& version : versions) {
            const int index = m_versionTabs->addTab(version);
            m_versionTabs->setTabData(index, version);
            m_versionTabs->setTabToolTip(index, version);
        }
        if (!versions.isEmpty())
            m_versionTabs->setCurrentIndex(0);
    }

    populateOfdTable();
}

void FileTypeReferenceDialog::populateOfdTable()
{
    if (!m_branchTabs || !m_versionTabs)
        return;

    const int branchIndex = m_branchTabs->currentIndex();
    const int versionIndex = m_versionTabs->currentIndex();
    const QString branch = branchIndex >= 0
            ? m_branchTabs->tabData(branchIndex).toString() : QString();
    const QString version = versionIndex >= 0
            ? m_versionTabs->tabData(versionIndex).toString() : QString();

    QList<const FileTypeReferenceRow*> rows;
    if (!branch.isEmpty() && !version.isEmpty()) {
        for (const FileTypeReferenceRow& row : m_ofdRows) {
            if (row.scope == branch && row.versions.contains(version))
                rows.append(&row);
        }
    }

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const FileTypeReferenceRow& data = *rows.at(row);
        m_table->setItem(row, 0, makeCodeItem(data));
        m_table->setItem(row, 1, makeNameItem(data));

        const QString sections = joinSections(data.sections);
        auto* sectionItem = new QTableWidgetItem(sections);
        sectionItem->setToolTip(sections);
        m_table->setItem(row, 2, sectionItem);
    }
    m_table->setUpdatesEnabled(true);

    if (m_countLabel)
        m_countLabel->setText(QStringLiteral("共 %1 项").arg(rows.size()));
}

void FileTypeReferenceDialog::fillWealthTable(const ConfigRepository& repository)
{
    QList<FileTypeReferenceRow> rows = buildWealthRows(repository);

    std::sort(rows.begin(), rows.end(),
              [](const FileTypeReferenceRow& a, const FileTypeReferenceRow& b) {
        if (a.fileNo.isEmpty() != b.fileNo.isEmpty())
            return !a.fileNo.isEmpty();
        const int fileNoCompare = a.fileNo.compare(b.fileNo, Qt::CaseInsensitive);
        if (fileNoCompare != 0)
            return fileNoCompare < 0;
        const int codeCompare = a.code.compare(b.code, Qt::CaseInsensitive);
        if (codeCompare != 0)
            return codeCompare < 0;
        const int scopeCompare = a.scope.compare(b.scope, Qt::CaseInsensitive);
        if (scopeCompare != 0)
            return scopeCompare < 0;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const FileTypeReferenceRow& data = rows.at(row);
        m_table->setItem(row, 0, makeCodeItem(data));
        m_table->setItem(row, 1, makeNameItem(data));

        const QString fileNo = data.fileNo.isEmpty()
                ? QStringLiteral("-") : data.fileNo;
        auto* fileNoItem = new QTableWidgetItem(fileNo);
        fileNoItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 2, fileNoItem);

        auto* versionItem = new QTableWidgetItem(joinVersions(data.versions));
        versionItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 3, versionItem);
    }
    m_table->setUpdatesEnabled(true);
}

void FileTypeReferenceDialog::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    int visible = 0;

    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool matched = needle.isEmpty();
        if (!matched) {
            for (int column = 0; column < m_table->columnCount(); ++column) {
                const QTableWidgetItem* item = m_table->item(row, column);
                if (item && item->text().contains(needle, Qt::CaseInsensitive)) {
                    matched = true;
                    break;
                }
            }
        }
        m_table->setRowHidden(row, !matched);
        if (matched)
            ++visible;
    }

    if (m_countLabel) {
        if (needle.isEmpty()) {
            m_countLabel->setText(QStringLiteral("共 %1 项")
                                          .arg(m_table->rowCount()));
        } else {
            m_countLabel->setText(QStringLiteral("显示 %1 / %2 项")
                                          .arg(visible)
                                          .arg(m_table->rowCount()));
        }
    }
}

} // namespace rfv
