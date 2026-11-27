#include "gui/filterheader.h"

#include "gui/filterpopup.h"

#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionHeader>

#include <utility>

namespace rfv {

namespace {

// 筛选按钮（▾）的尺寸与右边距
constexpr int kButtonSize = 15;
constexpr int kButtonMargin = 4;

// 处于筛选状态时按钮使用的强调色
const QColor kActiveColor(0x25, 0x63, 0xeb);

} // namespace

FilterHeaderView::FilterHeaderView(Qt::Orientation orientation, QWidget* parent)
    : QHeaderView(orientation, parent)
{
    setSectionsClickable(true);
    setHighlightSections(false);
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    setMouseTracking(true);
}

void FilterHeaderView::clearFilters()
{
    if (m_activeFilters.isEmpty() && m_pressedButton < 0)
        return;
    m_activeFilters.clear();
    m_pressedButton = -1;
    update();
}

QRect FilterHeaderView::sectionRect(int logicalIndex) const
{
    if (logicalIndex < 0 || logicalIndex >= count())
        return QRect();
    const int position = sectionViewportPosition(logicalIndex);
    const int size = sectionSize(logicalIndex);
    if (size <= 0)
        return QRect();
    return QRect(position, 0, size, height());
}

QRect FilterHeaderView::filterButtonRect(int logicalIndex) const
{
    const QRect section = sectionRect(logicalIndex);
    if (!section.isValid())
        return QRect();
    const int size = qMin(kButtonSize, section.height() - 6);
    if (size <= 0)
        return QRect();
    return QRect(section.right() - size - kButtonMargin,
                 section.center().y() - size / 2, size, size);
}

bool FilterHeaderView::isOnFilterButton(const QPoint& position, int* logicalIndex) const
{
    const int logical = logicalIndexAt(position);
    if (logical <= 0) {
        // 序号列不提供筛选
        if (logicalIndex)
            *logicalIndex = -1;
        return false;
    }
    if (logicalIndex)
        *logicalIndex = logical;
    return filterButtonRect(logical).contains(position);
}

int FilterHeaderView::fullHeaderSectionWidth(int logicalIndex) const
{
    // 列标题完整显示所需宽度 = 文本宽 + 左右样式边距 + 边框，
    // 非序号列再加上筛选按钮占用的宽度
    const QString title = model()
            ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
            : QString();
    const QFontMetrics metrics(font());
    const int margin = style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, this);
    int width = metrics.horizontalAdvance(title) + 2 * margin + 2;
    if (logicalIndex > 0)
        width += kButtonSize + 2 * kButtonMargin;
    return width;
}

void FilterHeaderView::paintSection(QPainter* painter, const QRect& rect,
                                    int logicalIndex) const
{
    if (!rect.isValid())
        return;

    // 与 QHeaderView::paintSection 基本一致的样式选项，
    // 区别在于标题文本先让出右侧筛选按钮占用的宽度再做省略。
    QStyleOptionHeader opt;
    initStyleOption(&opt);

    opt.rect = rect;
    opt.section = logicalIndex;
    opt.state |= QStyle::State_Raised;
    if (orientation() == Qt::Horizontal)
        opt.state |= QStyle::State_Horizontal;
    if (isEnabled())
        opt.state |= QStyle::State_Enabled;
    if (window() && window()->isActiveWindow())
        opt.state |= QStyle::State_Active;
    opt.sortIndicator = QStyleOptionHeader::None;

    // 位置决定首尾分隔线的绘制方式
    const int visual = visualIndex(logicalIndex);
    const int visibleCount = count() - hiddenSectionCount();
    if (visibleCount <= 1)
        opt.position = QStyleOptionHeader::OnlyOneSection;
    else if (visual <= 0)
        opt.position = QStyleOptionHeader::Beginning;
    else if (visual >= visibleCount - 1)
        opt.position = QStyleOptionHeader::End;
    else
        opt.position = QStyleOptionHeader::Middle;
    opt.selectedPosition = QStyleOptionHeader::NotAdjacent;

    const QVariant alignment = model()
            ? model()->headerData(logicalIndex, orientation(), Qt::TextAlignmentRole)
            : QVariant();
    opt.textAlignment = Qt::Alignment(alignment.isValid()
                                      ? alignment.toInt() : defaultAlignment());
    opt.text = model()
            ? model()->headerData(logicalIndex, orientation(), Qt::DisplayRole).toString()
            : QString();

    // 标题文本：为筛选按钮保留宽度后再省略（序号列没有按钮，不保留）。
    // QHeaderView 默认 ElideNone，这里给出有效的省略方向以保证文字不进入按钮区。
    // SE_HeaderLabel 返回的矩形已扣除样式边距，只需再减去按钮占用的宽度。
    const int reserved = logicalIndex > 0 ? kButtonSize + 2 * kButtonMargin : 0;
    if (reserved > 0) {
        const Qt::TextElideMode elideMode = textElideMode() != Qt::ElideNone
                ? textElideMode() : Qt::ElideRight;
        const QRect textRect = style()->subElementRect(QStyle::SE_HeaderLabel, &opt, this);
        opt.text = opt.fontMetrics.elidedText(
            opt.text, elideMode, qMax(0, textRect.width() - reserved));
    }

    // drawControl 可能遗留裁剪等 painter 状态，保存/恢复后再画按钮
    painter->save();
    style()->drawControl(QStyle::CE_Header, &opt, painter, this);
    painter->restore();

    // 序号列不提供筛选
    if (logicalIndex <= 0)
        return;

    // 列标题右侧的筛选按钮（Excel 式圆角小按钮，激活时高亮）
    const QRect buttonRect = filterButtonRect(logicalIndex);
    if (!buttonRect.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool active = m_activeFilters.contains(logicalIndex);
    const QColor fill = active ? QColor(0xdb, 0xe7, 0xfe) : QColor(0xf3, 0xf4, 0xf6);
    const QColor border = active ? kActiveColor : QColor(0x9c, 0xa3, 0xaf);
    const QColor arrow = active ? kActiveColor : QColor(0x4b, 0x55, 0x63);

    painter->setPen(QPen(border, 1));
    painter->setBrush(fill);
    painter->drawRoundedRect(QRectF(buttonRect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

    painter->setPen(Qt::NoPen);
    painter->setBrush(arrow);
    const QPointF center = QRectF(buttonRect).center();
    const qreal size = qMin(buttonRect.width(), buttonRect.height()) / 4.0;
    QPolygonF triangle;
    triangle << QPointF(center.x() - size, center.y() - size / 2)
             << QPointF(center.x() + size, center.y() - size / 2)
             << QPointF(center.x(), center.y() + size / 2);
    painter->drawPolygon(triangle);
    painter->restore();
}

void FilterHeaderView::mousePressEvent(QMouseEvent* event)
{
    int logical = -1;
    if (event->button() == Qt::LeftButton
        && isOnFilterButton(event->pos(), &logical)) {
        m_pressedButton = logical;
        return;
    }
    m_pressedButton = -1;
    QHeaderView::mousePressEvent(event);
}

void FilterHeaderView::mouseReleaseEvent(QMouseEvent* event)
{
    int logical = -1;
    if (m_pressedButton >= 0 && event->button() == Qt::LeftButton
        && isOnFilterButton(event->pos(), &logical)
        && logical == m_pressedButton) {
        m_pressedButton = -1;
        openPopup(logical);
        return;
    }
    m_pressedButton = -1;
    QHeaderView::mouseReleaseEvent(event);
}

void FilterHeaderView::mouseMoveEvent(QMouseEvent* event)
{
    const int logical = logicalIndexAt(event->pos());
    const bool overButton = logical > 0
            && filterButtonRect(logical).contains(event->pos());
    setCursor(overButton ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QHeaderView::mouseMoveEvent(event);
}

void FilterHeaderView::openPopup(int logicalIndex)
{
    auto* tableModel = qobject_cast<TableModel*>(model());
    if (!tableModel || logicalIndex <= 0)
        return;

    const int dataColumn = logicalIndex - 1;
    const ColumnFilter current = tableModel->columnFilter(dataColumn);

    const QStringList values = tableModel->distinctDisplayValues(dataColumn);
    const QSet<QString> checked = current.filterByValues
            ? current.values
            : QSet<QString>(values.cbegin(), values.cend());

    auto* popup = new FilterPopup(this);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setup(values, checked, current.text);

    connect(popup, &FilterPopup::applyRequested, this,
            [this, logicalIndex](const QString& text, const QSet<QString>& values,
                                 bool allSelected) {
        ColumnFilter filter;
        filter.text = text;
        filter.filterByValues = !allSelected;
        filter.values = values;
        if (filter.filterByValues || !filter.text.isEmpty())
            m_activeFilters.insert(logicalIndex);
        else
            m_activeFilters.remove(logicalIndex);
        update();
        emit filterChanged(logicalIndex, filter);
    });

    connect(popup, &FilterPopup::clearRequested, this, [this, logicalIndex]() {
        m_activeFilters.remove(logicalIndex);
        update();
        ColumnFilter filter;
        emit filterChanged(logicalIndex, filter);
    });

    // 弹在按钮下方，右对齐到列的右缘；越出主窗口时钳制到窗口内，
    // 下方放不下时改为从按钮上方弹出
    const QRect buttonRect = filterButtonRect(logicalIndex);
    const QPoint anchor = viewport()->mapToGlobal(buttonRect.bottomRight());
    const QRect windowRect = window()->frameGeometry();
    QPoint topLeft(anchor.x() - popup->width(), anchor.y() + 2);
    topLeft.setX(qBound(windowRect.left() + 4, topLeft.x(),
                        windowRect.right() - popup->width() - 4));
    if (topLeft.y() + popup->height() > windowRect.bottom() - 4) {
        // 从按钮上方弹出
        topLeft.setY(viewport()->mapToGlobal(buttonRect.topLeft()).y()
                     - popup->height() - 2);
    }
    topLeft.setY(qBound(windowRect.top() + 4, topLeft.y(),
                        windowRect.bottom() - popup->height() - 4));
    popup->move(topLeft);
    popup->show();
}

} // namespace rfv
