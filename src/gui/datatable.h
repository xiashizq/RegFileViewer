#pragma once

#include <QTableView>

namespace rfv {

class FilterHeaderView;

/// 数据表视图：在 QTableView 基础上增加
/// 1. 第 1 列“序号”冻结在左侧（覆盖在主视图左上角的独立子视图），
///    点击冻结区的序号可选中整行；
/// 2. 横向滚动时通知筛选表头重新摆放各列的筛选输入框。
class DataTable : public QTableView
{
    Q_OBJECT

public:
    explicit DataTable(QWidget* parent = nullptr);

    void setModel(QAbstractItemModel* model) override;
    void setFilterHeader(FilterHeaderView* header);
    FilterHeaderView* filterHeader() const { return m_filterHeader; }

protected:
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateFrozenGeometry();
    void syncFrozenScroll();
    void syncFrozenSelection();

    FilterHeaderView* m_filterHeader = nullptr;
    QTableView* m_frozenView = nullptr;
    bool m_syncingScroll = false;
    bool m_syncingSelection = false;
    bool m_syncingGeometry = false;
};

} // namespace rfv
