#include "gui/filterpopup.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace rfv {

namespace {

// “(全选)”条目的标记值（存于 Qt::UserRole）
const char kSelectAllTag[] = "__rfv_select_all__";
constexpr char kValueRole[] = "filterValue";

/// 列表条目显示文本：空值显示为“(空白)”，与 Excel 一致
QString itemLabel(const QString& value)
{
    return value.isEmpty() ? QStringLiteral("(空白)") : value;
}

} // namespace

FilterPopup::FilterPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup)
{
    setMinimumWidth(260);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_textEdit = new QLineEdit(this);
    m_textEdit->setPlaceholderText(QStringLiteral("文本筛选：包含…"));
    m_textEdit->setClearButtonEnabled(true);
    layout->addWidget(m_textEdit);

    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout();
    auto* clearButton = new QPushButton(QStringLiteral("清除筛选"), this);
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    auto* okButton = new QPushButton(QStringLiteral("确定"), this);
    okButton->setDefault(true);
    buttons->addWidget(okButton);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    buttons->addWidget(cancelButton);
    layout->addLayout(buttons);

    // “全选”条目与其余条目联动
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_updatingSelectAll)
            return;
        if (item->data(Qt::UserRole).toString() == QLatin1String(kSelectAllTag)) {
            const bool checked = item->checkState() == Qt::Checked;
            m_updatingSelectAll = true;
            for (int i = 1; i < m_list->count(); ++i)
                m_list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            m_updatingSelectAll = false;
        } else {
            updateSelectAllState();
        }
    });

    connect(okButton, &QPushButton::clicked, this, [this]() {
        QString text = m_textEdit->text().trimmed();
        QSet<QString> values;
        bool allSelected = true;
        for (int i = 1; i < m_list->count(); ++i) {
            const QListWidgetItem* item = m_list->item(i);
            if (item->checkState() == Qt::Checked) {
                values.insert(item->data(Qt::UserRole).toString());
            } else {
                allSelected = false;
            }
        }
        if (allSelected)
            values.clear();
        emit applyRequested(text, values, allSelected);
        close();
    });

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        emit clearRequested();
        close();
    });

    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);
}

void FilterPopup::setup(const QStringList& values, const QSet<QString>& checked,
                        const QString& text)
{
    m_textEdit->setText(text);

    m_list->clear();
    m_updatingSelectAll = true;

    auto* selectAll = new QListWidgetItem(QStringLiteral("(全选)"), m_list);
    selectAll->setData(Qt::UserRole, QString::fromLatin1(kSelectAllTag));
    selectAll->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    selectAll->setCheckState(checked.isEmpty() ? Qt::Checked : Qt::PartiallyChecked);
    QFont font = selectAll->font();
    font.setBold(true);
    selectAll->setFont(font);

    for (const QString& value : values) {
        auto* item = new QListWidgetItem(itemLabel(value), m_list);
        item->setData(Qt::UserRole, value);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(checked.contains(value) ? Qt::Checked : Qt::Unchecked);
    }

    updateSelectAllState();
    m_updatingSelectAll = false;

    // 高度随条目数量自适应，列表内部滚动
    const int rows = values.size() + 1;
    const int rowHeight = m_list->sizeHintForRow(0) > 0
            ? m_list->sizeHintForRow(0)
            : m_list->fontMetrics().height() + 8;
    m_list->setFixedHeight(qMin(360, rowHeight * qMin(rows, 18) + 4));
    adjustSize();
}

void FilterPopup::updateSelectAllState()
{
    if (m_list->count() <= 1)
        return;

    int checkedCount = 0;
    for (int i = 1; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked)
            ++checkedCount;
    }

    QListWidgetItem* selectAll = m_list->item(0);
    const Qt::CheckState state = checkedCount == 0 ? Qt::Unchecked
                                 : checkedCount == m_list->count() - 1
                                 ? Qt::Checked : Qt::PartiallyChecked;
    if (selectAll->checkState() != state) {
        // 程序化更新同样要防止重入联动处理器
        m_updatingSelectAll = true;
        selectAll->setCheckState(state);
        m_updatingSelectAll = false;
    }
}

} // namespace rfv
