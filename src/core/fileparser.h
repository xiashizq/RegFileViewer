#pragma once

#include "core/interface.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace rfv {

/// 打开文件时的初步探测结果，用于匹配解析方案。
struct FileProbe
{
    QString filePath;
    QString fileName;
    bool isOfd = false;
    bool isDelimited = false;      // 内部清算等竖线分隔文件
    QString version;       // OFD: 22 / 21 / 20
    QString fileType;      // OFD: 文件类型，例如 01 / C1
    QString firstLine;
    QStringList previewLines;
    QStringList headerFieldNames;   // OFD 文件头中声明的字段英文名
    int headerFieldCount = -1;
    QString delimiter;              // 分隔符，通常为 |
    QString encoding;               // 探测到的编码
    QStringList delimitedHeader;    // 分隔符文件的表头字段
    int delimitedDataBeginRow = 0;  // 分隔符文件的数据起始行（1 开始）
    QByteArray firstLineBytes;
    qint64 fileSize = 0;
};

/// 解析结果。数据行按原始字节保存，显示时再按字段切片解码，内存占用较小。
class Document
{
public:
    QString filePath;
    InterfaceDefinition definition;
    QStringList headerLines;
    QVector<QByteArray> rows;
    QVector<int> sourceLines;
    QStringList warnings;

    // 分隔符文件（内部清算）专用
    QVector<QStringList> values;
    QStringList preambleLines;
    QString headerRaw;
    QStringList tailLines;
    QString lineEnding = QStringLiteral("\r\n");
    bool endsWithNewline = true;
    bool hasBom = false;
    QString senderCode;
    QString receiverCode;
    QString fileDate;

    int declaredRecordCount = -1;
    bool sawEndMarker = false;
    int parseMilliseconds = 0;

    bool isDelimited() const { return definition.layout == Layout::Delimited; }
    int rowCount() const { return isDelimited() ? values.size() : rows.size(); }
    int fieldCount() const { return definition.fields.size(); }
    bool isEmpty() const { return rowCount() == 0; }

    QString value(int row, int fieldIndex) const;
    QStringList rowValues(int row) const;
    QString sourceLineText(int row) const;
};

class FileParser
{
public:
    static bool probe(const QString& path, FileProbe& probe, QString* error = nullptr);

    static bool parse(const QString& path, const InterfaceDefinition& definition,
                      Document& document, QString* error = nullptr);

    /// 解析“元数据行 + 表头行 + 竖线分隔数据行”的内部清算文件。
    static bool parseDelimited(const QString& path, Document& document,
                               QString* error = nullptr);

    static QByteArray readAllBytes(const QString& path, qint64 maxBytes = -1);
};

} // namespace rfv
