#include "gui/choosedialog.h"

#include "core/configrepository.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace rfv {

ChooseDialog::ChooseDialog(const QList<InterfaceDefinition>& candidates,
                           const FileProbe& probe, QWidget* parent)
    : QDialog(parent)
    , m_candidates(candidates)
{
    setWindowTitle(QStringLiteral("选择解析方案"));
    resize(980, 520);

    auto* layout = new QVBoxLayout(this);

    const QString intro = QStringLiteral(
                "文件：%1\n首行标记：%2    版本：%3    文件类型：%4\n"
                "共匹配到 %5 个候选解析方案，请选择：")
            .arg(probe.fileName, probe.firstLine,
                 probe.version.isEmpty() ? QStringLiteral("-") : probe.version,
                 probe.fileType.isEmpty() ? QStringLiteral("-") : probe.fileType)
            .arg(candidates.size());

    auto* label = new QLabel(intro, this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("协议")
        << QStringLiteral("版本")
        << QStringLiteral("接口说明")
        << QStringLiteral("配置段")
        << QStringLiteral("字段数")
        << QStringLiteral("字段匹配")
        << QStringLiteral("配置文件"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);

    m_table->setRowCount(candidates.size());
    for (int row = 0; row < candidates.size(); ++row) {
        const InterfaceDefinition& def = candidates.at(row);

        auto* protocol = new QTableWidgetItem(def.protocol);
        protocol->setToolTip(def.protocol);
        m_table->setItem(row, 0, protocol);

        m_table->setItem(row, 1, new QTableWidgetItem(def.versionLabel));
        m_table->setItem(row, 2, new QTableWidgetItem(def.describe));
        m_table->setItem(row, 3, new QTableWidgetItem(def.section));

        auto* fields = new QTableWidgetItem(QString::number(def.fieldCount));
        fields->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 4, fields);

        QString matchText = QStringLiteral("-");
        if (!probe.headerFieldNames.isEmpty()) {
            const int matched = headerFieldMatchCount(def, probe.headerFieldNames);
            const int comparable = qMin(probe.headerFieldNames.size(), def.fieldCount);
            matchText = QStringLiteral("%1/%2").arg(matched).arg(comparable);
        }
        auto* matchItem = new QTableWidgetItem(matchText);
        matchItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 5, matchItem);

        m_table->setItem(row, 6, new QTableWidgetItem(def.configFile));
    }

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(0, 260);

    if (!candidates.isEmpty())
        m_table->selectRow(0);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { accept(); });

    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("使用此方案解析"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

InterfaceDefinition ChooseDialog::selected() const
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_candidates.size())
        return m_candidates.value(0);
    return m_candidates.at(row);
}

} // namespace rfv
