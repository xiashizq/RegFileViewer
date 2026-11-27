#pragma once

#include <QSet>
#include <QWidget>

class QListWidget;
class QLineEdit;

namespace rfv {

/// Excel 式的列筛选悬浮弹窗：
/// 上方为“文本筛选”输入框（包含匹配），中间为去重值勾选列表（含“全选”），
/// 底部提供 清除筛选 / 确定 / 取消。
class FilterPopup : public QWidget
{
    Q_OBJECT

public:
    explicit FilterPopup(QWidget* parent = nullptr);

    void setup(const QStringList& values, const QSet<QString>& checked,
               const QString& text);

signals:
    /// 点击“确定”：values 为勾选保留的值，allSelected 为全部勾选（等同不过滤）
    void applyRequested(const QString& text, const QSet<QString>& values,
                        bool allSelected);
    /// 点击“清除筛选”
    void clearRequested();

private:
    void updateSelectAllState();

    QLineEdit* m_textEdit = nullptr;
    QListWidget* m_list = nullptr;
    bool m_updatingSelectAll = false;
};

} // namespace rfv
