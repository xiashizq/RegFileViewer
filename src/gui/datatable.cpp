#include "gui/datatable.h"

#include "gui/filterheader.h"

#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QScrollBar>

namespace rfv {

namespace {

/// Qt5 的 QItemSelectionRange 没有 4 参数构造，用两个模型索引构造一个“整行第 0 列”的范围
QItemSelectionRange serialColumnRange(QAbstractItemModel* model, int top, int bottom)
{
    if (!model)
        return QItemSelectionRange();
    return QItemSelectionRange(model->index(top, 0),
                               model->index(bottom, 0));
}

} // namespace

DataTable::DataTable(QWidget* parent)
    : QTableView(parent)
{
    // 冻结区：只显示第 1 列（序号），横向滚动时仍固定在左侧
    m_frozenView = new QTableView(this);
    m_frozenView->setFocusPolicy(Qt::NoFocus);
    m_frozenView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frozenView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frozenView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_frozenView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozenView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frozenView->setWordWrap(false);
    m_frozenView->verticalHeader()->hide();
    m_frozenView->verticalHeader()->setDefaultSectionSize(22);
    m_frozenView->horizontalHeader()->setDefaultSectionSize(72);
    m_frozenView->horizontalHeader()->setMinimumSectionSize(32);
    m_frozenView->horizontalHeader()->setSectionsClickable(false);
    m_frozenView->horizontalHeader()->setHighlightSections(false);
    m_frozenView->setStyleSheet(
        QStringLiteral("QTableView { border: none;"
                       " border-right: 1px solid #c9ced8; }"));
    m_frozenView->hide();

    // 纵向滚动联动（双向），用标志防止来回触发
    connect(m_frozenView->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if (m_syncingScroll)
            return;
        m_syncingScroll = true;
        verticalScrollBar()->setValue(value);
        m_syncingScroll = false;
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if (m_syncingScroll)
            return;
        m_syncingScroll = true;
        m_frozenView->verticalScrollBar()->setValue(value);
        m_syncingScroll = false;
    });

    // 点击冻结区的序号 -> 主视图选中整行
    connect(m_frozenView, &QTableView::pressed, this, [this](const QModelIndex& index) {
        if (index.isValid() && model())
            selectRow(index.row());
    });

    // 冻结区列宽变化（用户在冻结区表头拖拽）后同步主视图第 1 列
    connect(m_frozenView->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int logicalIndex, int, int newSize) {
        if (logicalIndex != 0 || m_syncingGeometry || !model())
            return;
        m_syncingGeometry = true;
        setColumnWidth(0, newSize);
        updateFrozenGeometry();
        m_syncingGeometry = false;
    });
}

void DataTable::setFilterHeader(FilterHeaderView* header)
{
    m_filterHeader = header;

    // 主视图第 1 列宽度变化（列宽自适应/用户拖拽）后同步冻结区。
    // 注意主表头可能已被替换为筛选表头，因此这里用当前的表头对象连接。
    if (horizontalHeader() && m_filterHeader) {
        connect(horizontalHeader(), &QHeaderView::sectionResized, this,
                [this](int logicalIndex, int, int) {
            if (logicalIndex != 0 || m_syncingGeometry || !m_frozenView)
                return;
            m_syncingGeometry = true;
            m_frozenView->setColumnWidth(0, columnWidth(0));
            updateFrozenGeometry();
            m_syncingGeometry = false;
        });
    }

    updateFrozenGeometry();
}

void DataTable::setModel(QAbstractItemModel* model)
{
    QTableView::setModel(model);

    m_frozenView->setModel(model);

    if (model) {
        connect(model, &QAbstractItemModel::modelReset,
                this, &DataTable::updateFrozenGeometry);
    }

    // 主视图选择 <-> 冻结区选择联动
    if (selectionModel() && m_frozenView->selectionModel()) {
        connect(selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection&, const QItemSelection&) {
            syncFrozenSelection();
        });
        connect(m_frozenView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection&, const QItemSelection&) {
            if (m_syncingSelection || !selectionModel())
                return;
            m_syncingSelection = true;
            selectionModel()->clearSelection();
            const QItemSelection selected = m_frozenView->selectionModel()->selection();
            QItemSelection rows;
            for (const QItemSelectionRange& range : selected) {
                rows.append(serialColumnRange(selectionModel()->model(),
                                              range.top(), range.bottom()));
            }
            selectionModel()->select(rows, QItemSelectionModel::Select);
            m_syncingSelection = false;
        });
        syncFrozenSelection();
    }

    updateFrozenGeometry();
}

void DataTable::scrollContentsBy(int dx, int dy)
{
    QTableView::scrollContentsBy(dx, dy);
    syncFrozenScroll();
}

void DataTable::resizeEvent(QResizeEvent* event)
{
    QTableView::resizeEvent(event);
    updateFrozenGeometry();
}

void DataTable::showEvent(QShowEvent* event)
{
    QTableView::showEvent(event);
    updateFrozenGeometry();
}

void DataTable::updateFrozenGeometry()
{
    if (!m_frozenView)
        return;

    const int columns = model() ? model()->columnCount() : 0;
    if (columns == 0) {
        m_frozenView->hide();
        return;
    }

    const int width = columnWidth(0);
    const int height = horizontalHeader()->height() + viewport()->height();
    m_frozenView->setGeometry(frameWidth(), frameWidth(), width, height);
    m_frozenView->raise();
    m_frozenView->show();
}

void DataTable::syncFrozenScroll()
{
    if (!m_frozenView || m_syncingScroll)
        return;
    m_syncingScroll = true;
    m_frozenView->verticalScrollBar()->setValue(verticalScrollBar()->value());
    m_syncingScroll = false;
}

void DataTable::syncFrozenSelection()
{
    if (!m_frozenView->selectionModel() || !selectionModel() || m_syncingSelection)
        return;
    m_syncingSelection = true;
    const QItemSelection selected = selectionModel()->selection();
    QItemSelection frozen;
    for (const QItemSelectionRange& range : selected) {
        frozen.append(serialColumnRange(m_frozenView->model(),
                                        range.top(), range.bottom()));
    }
    m_frozenView->selectionModel()->clearSelection();
    m_frozenView->selectionModel()->select(frozen, QItemSelectionModel::Select);
    m_syncingSelection = false;
}

} // namespace rfv
