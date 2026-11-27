#pragma once

#include "core/tablemodel.h"

#include <QHeaderView>
#include <QSet>

namespace rfv {

/// Excel 式的水平表头：每个列标题右侧绘制一个筛选按钮（▾），
/// 点击按钮弹出悬浮筛选面板（文本筛选 + 去重值勾选列表）。
/// 列处于筛选状态时按钮高亮显示。
class FilterHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    explicit FilterHeaderView(Qt::Orientation orientation, QWidget* parent = nullptr);

    void clearFilters();

    /// 列标题完整显示（不被按钮挤压省略）所需的表头宽度，
    /// 含筛选按钮占用的宽度，供列宽自适应取 max(表头, 内容) 使用
    int fullHeaderSectionWidth(int logicalIndex) const;

signals:
    /// 确定筛选后发出（filterByValues 为 false 且 text 为空表示该列无筛选）
    void filterChanged(int logicalIndex, const rfv::ColumnFilter& filter);

protected:
    void paintSection(QPainter* painter, const QRect& rect,
                      int logicalIndex) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QRect sectionRect(int logicalIndex) const;
    QRect filterButtonRect(int logicalIndex) const;
    bool isOnFilterButton(const QPoint& position, int* logicalIndex = nullptr) const;
    void openPopup(int logicalIndex);

    QSet<int> m_activeFilters;  // 处于筛选状态的逻辑列
    int m_pressedButton = -1;   // 按下时命中的筛选按钮（逻辑列索引）
};

} // namespace rfv
