#include "gui/infopanel.h"

#include "core/configrepository.h"

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

namespace {

QLabel* makeChip(const QString& text, const char* type)
{
    auto* chip = new QLabel(text);
    chip->setProperty("chipType", QString::fromLatin1(type));
    chip->setAlignment(Qt::AlignCenter);
    chip->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    return chip;
}

QFrame* makeCard(QVBoxLayout* parentLayout, const QString& title, QGridLayout** grid)
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("card"));

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(13, 11, 13, 12);
    cardLayout->setSpacing(8);

    if (!title.isEmpty()) {
        auto* titleLabel = new QLabel(title, card);
        titleLabel->setProperty("role", QStringLiteral("section"));
        cardLayout->addWidget(titleLabel);
    }

    auto* rows = new QGridLayout;
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setHorizontalSpacing(10);
    rows->setVerticalSpacing(7);
    rows->setColumnMinimumWidth(0, 62);
    rows->setColumnStretch(1, 1);
    cardLayout->addLayout(rows);

    parentLayout->addWidget(card);
    *grid = rows;
    return card;
}

int addRow(QGridLayout* grid, int row, const QString& key, const QString& value,
           bool monospace = false, bool selectable = true)
{
    auto* keyLabel = new QLabel(key);
    keyLabel->setProperty("role", QStringLiteral("key"));
    keyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto* valueLabel = new QLabel(value);
    valueLabel->setProperty("role", QStringLiteral("value"));
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(selectable
                                        ? Qt::TextSelectableByMouse
                                        : Qt::NoTextInteraction);

    if (monospace) {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(qMax(7.5, font.pointSizeF() - 0.5));
        valueLabel->setFont(font);
    }

    grid->addWidget(keyLabel, row, 0, Qt::AlignTop);
    grid->addWidget(valueLabel, row, 1);
    return row + 1;
}

QString protocolCategory(const rfv::InterfaceDefinition& definition)
{
    if (definition.layout == rfv::Layout::Delimited)
        return QStringLiteral("内部清算");
    if (definition.protocol.contains(QStringLiteral("理财")))
        return QStringLiteral("理财登");
    if (definition.protocol.contains(QStringLiteral("中央数据交换")))
        return QStringLiteral("中登基金");
    if (definition.protocol.contains(QStringLiteral("管理人")))
        return QStringLiteral("管理人TXT");
    return QStringLiteral("接口");
}

QString elided(const QString& text, const QFont& font, int width)
{
    return QFontMetrics(font).elidedText(text, Qt::ElideMiddle, width);
}

} // namespace

namespace rfv {

InfoPanel::InfoPanel(QWidget* parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("infoScroll"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setStyleSheet(QStringLiteral(R"(
        QScrollArea#infoScroll { background: transparent; border: none; }
        QWidget#infoContainer { background: transparent; }
        QFrame#card {
            background: #ffffff;
            border: 1px solid #e3e7ee;
            border-radius: 9px;
        }
        QLabel[role="section"] {
            color: #8b94a5;
            font-size: 8pt;
            font-weight: bold;
        }
        QLabel[role="key"] { color: #7c8596; font-size: 9pt; }
        QLabel[role="value"] { color: #1f2532; font-size: 9pt; }
        QLabel[role="filename"] { color: #111827; font-size: 11pt; font-weight: bold; }
        QLabel[role="path"] { color: #9aa3b2; font-size: 8pt; }
        QLabel[role="placeholder-title"] { color: #6b7484; font-size: 10pt; font-weight: bold; }
        QLabel[role="placeholder-text"] { color: #9aa3b2; font-size: 9pt; }
        QLabel[chipType="protocol"] {
            background: #e8f0fe; color: #1a56db;
            border-radius: 9px; padding: 2px 9px; font-size: 8pt;
        }
        QLabel[chipType="version"] {
            background: #f2f4f7; color: #48546a;
            border-radius: 9px; padding: 2px 9px; font-size: 8pt;
        }
        QLabel[chipType="success"] {
            background: #e6f6ec; color: #0f7b3f;
            border-radius: 9px; padding: 2px 9px; font-size: 8pt;
        }
        QLabel[chipType="warning"] {
            background: #fff3e0; color: #b25e09;
            border-radius: 9px; padding: 2px 9px; font-size: 8pt;
        }
        QLabel[chipType="muted"] {
            background: #f2f4f7; color: #98a1b0;
            border-radius: 9px; padding: 2px 9px; font-size: 8pt;
        }
        QToolButton#closeButton {
            border: none;
            background: transparent;
            color: #9aa3b2;
            font-size: 12pt;
            padding: 0;
        }
        QToolButton#closeButton:hover {
            color: #475467;
            background: #f2f4f7;
            border-radius: 9px;
        }
        QPushButton#linkButton {
            color: #1a56db;
            background: transparent;
            border: none;
            padding: 0;
            text-align: left;
            font-size: 9pt;
        }
        QPushButton#linkButton:hover { color: #0b3fa8; text-decoration: underline; }
        QScrollBar:vertical {
            width: 8px; background: transparent; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #c7cdd8; border-radius: 4px; min-height: 28px;
        }
        QScrollBar::handle:vertical:hover { background: #aeb6c4; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
    )"));

    m_container = new QWidget;
    m_container->setObjectName(QStringLiteral("infoContainer"));
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(13, 13, 13, 13);
    m_layout->setSpacing(10);
    setWidget(m_container);

    clear();
}

void InfoPanel::clearContent()
{
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            delete widget;
        delete item;
    }
}

void InfoPanel::clear()
{
    clearContent();
    addPlaceholder();
}

void InfoPanel::addPlaceholder()
{
    m_layout->addSpacing(48);

    auto* title = new QLabel(QStringLiteral("未打开文件"));
    title->setProperty("role", QStringLiteral("placeholder-title"));
    title->setAlignment(Qt::AlignHCenter);
    m_layout->addWidget(title);

    auto* hint = new QLabel(QStringLiteral(
        "将中登 OFD 文件、理财登定长文件\n或内部清算文件拖入窗口，或按 Ctrl+O 选择。"));
    hint->setProperty("role", QStringLiteral("placeholder-text"));
    hint->setAlignment(Qt::AlignHCenter);
    hint->setWordWrap(true);
    m_layout->addWidget(hint);

    m_layout->addStretch(1);
}

void InfoPanel::setDocument(const Document* document, const ConfigRepository* repository)
{
    clearContent();
    if (!document) {
        addPlaceholder();
        return;
    }

    const InterfaceDefinition& definition = document->definition;
    const QFileInfo fileInfo(document->filePath);
    const bool hasWarnings = !document->warnings.isEmpty();
    const bool hasData = document->rowCount() > 0;

    // ---- 头部：文件名 + 徽章 ------------------------------------------------
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("card"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(13, 12, 13, 12);
    headerLayout->setSpacing(7);

    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);

    auto* icon = new QLabel;
    icon->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(22, 22));
    icon->setFixedSize(22, 22);
    icon->setAlignment(Qt::AlignTop);

    auto* nameLabel = new QLabel(fileInfo.fileName());
    nameLabel->setProperty("role", QStringLiteral("filename"));
    nameLabel->setWordWrap(true);
    nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nameLabel->setToolTip(document->filePath);

    auto* closeButton = new QToolButton;
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setText(QStringLiteral("×"));
    closeButton->setToolTip(QStringLiteral("隐藏详情面板（Ctrl+I）"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(20, 20);
    connect(closeButton, &QToolButton::clicked, this, &InfoPanel::closeRequested);

    titleRow->addWidget(icon, 0, Qt::AlignTop);
    titleRow->addWidget(nameLabel, 1);
    titleRow->addWidget(closeButton, 0, Qt::AlignTop);
    headerLayout->addLayout(titleRow);

    auto* pathLabel = new QLabel;
    pathLabel->setProperty("role", QStringLiteral("path"));
    pathLabel->setText(elided(fileInfo.absolutePath(), pathLabel->font(), 300));
    pathLabel->setToolTip(fileInfo.absolutePath());
    headerLayout->addWidget(pathLabel);

    auto* chips = new QHBoxLayout;
    chips->setSpacing(6);
    chips->addWidget(makeChip(protocolCategory(definition), "protocol"));
    if (!definition.versionLabel.isEmpty())
        chips->addWidget(makeChip(definition.versionLabel, "version"));

    if (!hasData)
        chips->addWidget(makeChip(QStringLiteral("无数据"), "muted"));
    else if (hasWarnings)
        chips->addWidget(makeChip(QStringLiteral("%1 条提示").arg(document->warnings.size()),
                                  "warning"));
    else
        chips->addWidget(makeChip(QStringLiteral("解析成功"), "success"));
    chips->addStretch(1);
    headerLayout->addLayout(chips);

    m_layout->addWidget(header);

    // ---- 接口信息 -----------------------------------------------------------
    QGridLayout* interfaceGrid = nullptr;
    makeCard(m_layout, QStringLiteral("接口信息"), &interfaceGrid);
    int row = 0;
    row = addRow(interfaceGrid, row, QStringLiteral("接口"), definition.describe);
    row = addRow(interfaceGrid, row, QStringLiteral("协议"), definition.protocol);
    row = addRow(interfaceGrid, row,
                 definition.isDelimited() ? QStringLiteral("数据文件")
                                          : QStringLiteral("配置文件"),
                 definition.configFile, true);
    if (definition.isOfd())
        row = addRow(interfaceGrid, row, QStringLiteral("配置段"), definition.section, true);
    else if (definition.isDelimited())
        row = addRow(interfaceGrid, row, QStringLiteral("分隔符"), definition.delimiter, true);

    // ---- 报文头（OFD） -------------------------------------------------------
    if (definition.isOfd() && document->headerLines.size() >= 7) {
        const QString sender = document->headerLines.at(2).trimmed();
        const QString receiver = document->headerLines.at(3).trimmed();
        const QString date = document->headerLines.at(4).trimmed();
        const QString type = document->headerLines.at(6).trimmed();

        const QString senderName = repository ? repository->codeName(sender) : QString();
        const QString receiverName = repository ? repository->codeName(receiver) : QString();

        QGridLayout* headerGrid = nullptr;
        makeCard(m_layout, QStringLiteral("报文头"), &headerGrid);
        row = 0;
        row = addRow(headerGrid, row, QStringLiteral("文件类型"), type);
        row = addRow(headerGrid, row, QStringLiteral("发送方"),
                     senderName.isEmpty() ? sender
                                          : QStringLiteral("%1 · %2").arg(sender, senderName));
        row = addRow(headerGrid, row, QStringLiteral("接收方"),
                     receiverName.isEmpty() ? receiver
                                            : QStringLiteral("%1 · %2").arg(receiver, receiverName));
        row = addRow(headerGrid, row, QStringLiteral("文件日期"), date);
    }

    // ---- 数据概况 -----------------------------------------------------------
    QGridLayout* statsGrid = nullptr;
    makeCard(m_layout, QStringLiteral("数据概况"), &statsGrid);
    row = 0;
    row = addRow(statsGrid, row, QStringLiteral("记录数"),
                 document->declaredRecordCount >= 0
                 ? QStringLiteral("声明 %1 · 实际 %2")
                   .arg(document->declaredRecordCount).arg(document->rowCount())
                 : QString::number(document->rowCount()));
    row = addRow(statsGrid, row, QStringLiteral("字段数"), QString::number(definition.fieldCount));
    if (definition.isDelimited()) {
        row = addRow(statsGrid, row, QStringLiteral("分隔符"), definition.delimiter);
    } else {
        row = addRow(statsGrid, row, QStringLiteral("行长度"),
                     QStringLiteral("%1 %2").arg(definition.rowLength)
                     .arg(definition.lengthType == 0 ? QStringLiteral("字节")
                                                     : QStringLiteral("字符")));
    }
    row = addRow(statsGrid, row, QStringLiteral("编码"), definition.encoding);
    row = addRow(statsGrid, row, QStringLiteral("布局"), layoutLabel(definition.layout));
    if (definition.lastRowCheck.isEmpty()) {
        row = addRow(statsGrid, row, QStringLiteral("结束标识"),
                     QStringLiteral("不适用"));
    } else {
        row = addRow(statsGrid, row, QStringLiteral("结束标识"),
                     document->sawEndMarker
                     ? QStringLiteral("已找到 · %1").arg(definition.lastRowCheck)
                     : QStringLiteral("未找到"));
    }
    row = addRow(statsGrid, row, QStringLiteral("耗时"),
                 QStringLiteral("%1 ms").arg(document->parseMilliseconds));

    // ---- 诊断提示 -----------------------------------------------------------
    if (hasWarnings) {
        QGridLayout* warningGrid = nullptr;
        makeCard(m_layout, QStringLiteral("诊断"), &warningGrid);

        QString first = document->warnings.first();
        first.replace(QLatin1Char('\n'), QStringLiteral("  "));
        if (first.size() > 90)
            first = first.left(90) + QStringLiteral("…");

        auto* summary = new QLabel(first);
        summary->setProperty("role", QStringLiteral("value"));
        summary->setWordWrap(true);
        warningGrid->addWidget(summary, 0, 0, 1, 2);

        auto* button = new QPushButton(
                    document->warnings.size() > 1
                    ? QStringLiteral("查看全部 %1 条提示 →").arg(document->warnings.size())
                    : QStringLiteral("查看诊断详情 →"));
        button->setObjectName(QStringLiteral("linkButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        connect(button, &QPushButton::clicked, this, &InfoPanel::diagnosticsRequested);
        warningGrid->addWidget(button, 1, 0, 1, 2);
    }

    m_layout->addStretch(1);
}

} // namespace rfv
