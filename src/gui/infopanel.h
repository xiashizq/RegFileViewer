#pragma once

#include "core/fileparser.h"

#include <QScrollArea>

class QVBoxLayout;

namespace rfv {

class ConfigRepository;

/// 左侧文件详情面板：卡片式布局，展示文件摘要、报文头、数据概况与诊断提示。
class InfoPanel : public QScrollArea
{
    Q_OBJECT

public:
    explicit InfoPanel(QWidget* parent = nullptr);

    void setDocument(const Document* document, const ConfigRepository* repository);
    void clear();

signals:
    void diagnosticsRequested();
    void closeRequested();

private:
    void clearContent();
    void addPlaceholder();

    QWidget* m_container = nullptr;
    QVBoxLayout* m_layout = nullptr;
};

} // namespace rfv
