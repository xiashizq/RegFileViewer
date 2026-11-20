#include "core/fileparser.h"

#include "core/encoding.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

namespace {

QList<QByteArray> splitLines(const QByteArray& data)
{
    QList<QByteArray> lines;
    int start = 0;
    for (int i = 0; i < data.size(); ++i) {
        if (data.at(i) == '\n') {
            QByteArray line = data.mid(start, i - start);
            if (line.endsWith('\r'))
                line.chop(1);
            lines.append(line);
            start = i + 1;
        }
    }
    if (start < data.size()) {
        QByteArray line = data.mid(start);
        if (line.endsWith('\r'))
            line.chop(1);
        lines.append(line);
    }
    return lines;
}

QByteArray stripLineEnding(QByteArray line)
{
    while (line.endsWith('\n') || line.endsWith('\r'))
        line.chop(1);
    return line;
}

QString detectTextEncoding(const QByteArray& data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QStringLiteral("UTF-8");

    const QString utf8 = QString::fromUtf8(data);
    if (!utf8.contains(QChar::ReplacementCharacter))
        return QStringLiteral("UTF-8");
    return QStringLiteral("GB18030");
}

bool isAllDigits(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

/// 一行中有多少列是“非空且不是纯数字”的文本列。
/// 真正的表头通常是整行文本，而元数据/数据行里的编号、金额、日期多为数字。
int textFieldCount(const QStringList& fields)
{
    int count = 0;
    for (const QString& field : fields) {
        const QString text = field.trimmed();
        if (!text.isEmpty() && !isAllDigits(text))
            ++count;
    }
    return count;
}

/// 返回表头所在行（0 开始），找不到返回 -1。
/// 表头必须满足：至少 2 列、包含文本列、下一非空行与表头列数一致，
/// 且文本列数不少于下一行（数据行），避免把带字母代码的元数据行误认为表头。
int findDelimitedHeader(const QStringList& lines, QStringList* headerFields)
{
    const int scanCount = qMin(lines.size(), 12);
    for (int i = 0; i < scanCount; ++i) {
        const QString line = lines.at(i);
        if (!line.contains(QLatin1Char('|')))
            continue;

        const QStringList fields = line.split(QLatin1Char('|'));
        if (fields.size() < 2)
            continue;

        const int textFields = textFieldCount(fields);
        if (textFields == 0)
            continue;

        int next = i + 1;
        while (next < lines.size() && lines.at(next).trimmed().isEmpty())
            ++next;
        if (next >= lines.size())
            continue;

        const QStringList nextFields = lines.at(next).split(QLatin1Char('|'));
        if (nextFields.size() != fields.size())
            continue;
        if (textFields < textFieldCount(nextFields))
            continue;

        if (headerFields)
            *headerFields = fields;
        return i;
    }
    return -1;
}

/// 判断一行是否为 OFD“字段名后的记录数行”（例如 00035397）。
/// 这类行只有数字，且长度远小于固定长度的数据行。
bool parseOfdRecordCountLine(const QByteArray& line, int rowLength, int* value)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 20)
        return false;
    for (char ch : trimmed) {
        if (ch < '0' || ch > '9')
            return false;
    }
    if (rowLength > 0 && trimmed.size() >= rowLength)
        return false;

    bool ok = false;
    const int parsed = trimmed.toInt(&ok);
    if (!ok)
        return false;
    if (value)
        *value = parsed;
    return true;
}

} // namespace

QByteArray rfv::FileParser::readAllBytes(const QString& path, qint64 maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    if (maxBytes > 0)
        return file.read(maxBytes);
    return file.readAll();
}

bool rfv::FileParser::probe(const QString& path, FileProbe& probe, QString* error)
{
    probe = FileProbe();
    probe.filePath = path;
    probe.fileName = QFileInfo(path).fileName();
    probe.fileSize = QFileInfo(path).size();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开文件：%1").arg(path);
        return false;
    }

    const QByteArray head = file.read(64 * 1024);
    file.close();

    if (head.isEmpty()) {
        if (error)
            *error = QStringLiteral("文件为空：%1").arg(path);
        return false;
    }

    QByteArray firstRaw = head;
    if (firstRaw.startsWith("\xEF\xBB\xBF"))
        firstRaw.remove(0, 3);

    const QList<QByteArray> lines = splitLines(firstRaw);
    if (lines.isEmpty())
        return false;

    probe.encoding = detectTextEncoding(firstRaw);

    const int previewCount = qMin(lines.size(), 12);
    for (int i = 0; i < previewCount; ++i)
        probe.previewLines.append(decodeText(lines.at(i), probe.encoding));

    probe.firstLineBytes = lines.at(0);
    probe.firstLine = decodeText(lines.at(0), probe.encoding).trimmed();

    if (probe.firstLine.compare(QLatin1String("OFDCFDAT"), Qt::CaseInsensitive) == 0) {
        probe.isOfd = true;
        if (lines.size() > 1)
            probe.version = normalizeVersion(decodeText(lines.at(1), QStringLiteral("GB18030")));
        if (lines.size() > 6)
            probe.fileType = decodeText(lines.at(6), QStringLiteral("GB18030")).trimmed();

        // 第 10 行是字段总数，第 11 行起是字段英文名，可用于判断哪套配置最匹配
        if (lines.size() > 9) {
            bool ok = false;
            const int declared = decodeText(lines.at(9), QStringLiteral("GB18030"))
                    .trimmed().toInt(&ok);
            if (ok && declared > 0 && declared < 2000) {
                probe.headerFieldCount = declared;
                for (int i = 0; i < declared; ++i) {
                    const int index = 10 + i;
                    if (index >= lines.size())
                        break;
                    probe.headerFieldNames.append(
                                decodeText(lines.at(index), QStringLiteral("GB18030")).trimmed());
                }
            }
        }
        return true;
    }

    // 分隔符文件（内部清算）：元数据行 + 竖线表头 + 数据行
    QStringList decodedLines;
    decodedLines.reserve(previewCount);
    for (int i = 0; i < previewCount; ++i)
        decodedLines.append(decodeText(lines.at(i), probe.encoding));

    QStringList delimitedHeader;
    const int headerIndex = findDelimitedHeader(decodedLines, &delimitedHeader);
    if (headerIndex >= 0) {
        probe.isDelimited = true;
        probe.delimiter = QStringLiteral("|");
        probe.delimitedHeader = delimitedHeader;
        probe.delimitedDataBeginRow = headerIndex + 2;
        return true;
    }

    // 定长文件：第一行是文件标识，第二行是版本号
    if (lines.size() > 1)
        probe.version = normalizeVersion(decodeText(lines.at(1), probe.encoding).trimmed());

    return true;
}

bool rfv::FileParser::parse(const QString& path, const InterfaceDefinition& definition,
                            Document& document, QString* error)
{
    QElapsedTimer timer;
    timer.start();

    document = Document();
    document.filePath = path;
    document.definition = definition;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开文件：%1").arg(path);
        return false;
    }

    const QString encoding = definition.encoding.isEmpty()
            ? QStringLiteral("GB18030") : definition.encoding;
    const QByteArray endMarker = definition.lastRowCheck.toLatin1();
    const QByteArray ofdEndMarker("OFDCFEND");

    int lineNumber = 0;
    QVector<int> badLengthLines;
    QVector<int> badLengthActuals;
    int firstDataActualLength = -1;

    while (!file.atEnd()) {
        QByteArray line = stripLineEnding(file.readLine());
        ++lineNumber;

        if (lineNumber == 1 && line.startsWith("\xEF\xBB\xBF"))
            line.remove(0, 3);

        if (lineNumber < definition.dataBeginRow) {
            document.headerLines.append(decodeText(line, encoding));
            continue;
        }

        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        if (!endMarker.isEmpty() && trimmed.compare(endMarker, Qt::CaseInsensitive) == 0) {
            document.sawEndMarker = true;
            break;
        }
        if (definition.isOfd() && trimmed.compare(ofdEndMarker, Qt::CaseInsensitive) == 0) {
            document.sawEndMarker = true;
            break;
        }

        const int actualLength = definition.lengthType == 0
                ? line.size()
                : decodeText(line, encoding).size();

        if (document.rows.isEmpty())
            firstDataActualLength = actualLength;

        if (actualLength != definition.rowLength && definition.rowLength > 0) {
            badLengthLines.append(lineNumber);
            badLengthActuals.append(actualLength);
            if (actualLength < definition.rowLength && definition.lengthType == 0) {
                line.append(QByteArray(definition.rowLength - actualLength, ' '));
            } else if (actualLength > definition.rowLength && definition.lengthType == 0) {
                line.truncate(definition.rowLength);
            }
        }

        document.rows.append(line);
        document.sourceLines.append(lineNumber);
    }
    file.close();

    // 部分 OFD 文件在字段名之后、数据行之前多一行“总记录数”（例如 00035397）。
    // 该行只有数字且长度远小于数据行，按记录数行处理，不能当作数据行展示；
    // 这里把它并入文件头，保存时再同步更新。
    bool removedRecordCountLine = false;
    int recordCountLineValue = -1;
    if (definition.isOfd() && !document.rows.isEmpty()
        && definition.rowLength > 0
        && firstDataActualLength >= 0
        && firstDataActualLength < definition.rowLength) {
        int countValue = -1;
        if (parseOfdRecordCountLine(document.rows.first(), definition.rowLength, &countValue)) {
            const int countLine = document.sourceLines.first();
            // 保留字段名与记录数行之间可能存在的空行，保证文件头行号不错位
            for (int line = definition.dataBeginRow; line < countLine; ++line)
                document.headerLines.append(QString());
            document.headerLines.append(
                        decodeText(document.rows.first().trimmed(), encoding));
            document.rows.removeFirst();
            document.sourceLines.removeFirst();
            document.definition.dataBeginRow = countLine + 1;
            document.definition.dataRecordCountRow = countLine;
            recordCountLineValue = countValue;
            removedRecordCountLine = true;

            // 该行不是数据行，移除它产生的长度异常记录
            for (int i = badLengthLines.size() - 1; i >= 0; --i) {
                if (badLengthLines.at(i) == countLine) {
                    badLengthLines.remove(i);
                    badLengthActuals.remove(i);
                }
            }
        }
    }

    // 声明的记录数
    const int recordRow = definition.isOfd() ? 6 : definition.rowCountCheckRow;
    if (recordRow > 0 && document.headerLines.size() >= recordRow) {
        bool ok = false;
        const int declared = document.headerLines.at(recordRow - 1).trimmed().toInt(&ok);
        if (ok)
            document.declaredRecordCount = declared;
    }
    if (removedRecordCountLine)
        document.declaredRecordCount = recordCountLineValue;

    // 版本校验
    if (definition.versionCheckRow > 0
        && document.headerLines.size() >= definition.versionCheckRow) {
        const QString actual = normalizeVersion(
                    document.headerLines.at(definition.versionCheckRow - 1).trimmed());
        QString expected = definition.versionCheckValue;
        if (expected.isEmpty())
            expected = definition.version;
        expected = normalizeVersion(expected);
        const bool match = definition.versionCheckMode > 0
                ? actual.contains(expected)
                : actual == expected;
        if (!match) {
            document.warnings.append(QStringLiteral(
                "版本号不符：配置文件要求 %1，文件中为 %2（第 %3 行）")
                .arg(expected, actual).arg(definition.versionCheckRow));
        }
    }

    // 字段总数校验（定长文件）
    if (!definition.isOfd() && definition.fieldCountCheckRow > 0
        && document.headerLines.size() >= definition.fieldCountCheckRow) {
        bool ok = false;
        const int actualCount = document.headerLines
                .at(definition.fieldCountCheckRow - 1).trimmed().toInt(&ok);
        if (ok && actualCount != definition.fieldCount) {
            document.warnings.append(QStringLiteral(
                "字段数不一致：配置文件为 %1，文件头声明为 %2")
                .arg(definition.fieldCount).arg(actualCount));
        }
    }

    // OFD 文件头中的字段英文名校验，可发现接口版本升级导致的字段漂移
    if (definition.isOfd()) {
        if (document.headerLines.size() > 9) {
            bool ok = false;
            const int headerFieldCount = document.headerLines.at(9).trimmed().toInt(&ok);
            if (ok && headerFieldCount != definition.fieldCount) {
                document.warnings.append(QStringLiteral(
                    "字段数不一致：配置文件为 %1，文件头声明为 %2；"
                    "请确认接口版本是否已更新")
                    .arg(definition.fieldCount).arg(headerFieldCount));
            }
        }

        QStringList mismatches;
        const int nameBegin = 10; // 第 11 行开始是字段英文名
        for (int i = 0; i < definition.fields.size(); ++i) {
            const int index = nameBegin + i;
            if (index >= document.headerLines.size())
                break;
            const QString actual = document.headerLines.at(index).trimmed();
            const QString expected = definition.fields.at(i).nameEn;
            if (!actual.isEmpty() && !expected.isEmpty()
                && actual.compare(expected, Qt::CaseInsensitive) != 0) {
                if (mismatches.size() < 5) {
                    mismatches.append(QStringLiteral("第%1个字段：配置=%2 / 文件=%3")
                                      .arg(i + 1).arg(expected, actual));
                }
            }
        }
        if (!mismatches.isEmpty()) {
            document.warnings.append(QStringLiteral(
                "文件头字段名与解析配置不一致，数据可能错位：\n  %1")
                .arg(mismatches.join(QStringLiteral("\n  "))));
        }
    }

    if (!badLengthLines.isEmpty()) {
        document.warnings.append(QStringLiteral(
            "有 %1 行的实际长度与配置的 %2 不一致（首个异常在第 %3 行，实际长度 %4），"
            "已按配置长度补空格/截断")
            .arg(badLengthLines.size()).arg(definition.rowLength)
            .arg(badLengthLines.first()).arg(badLengthActuals.first()));
    }

    if (document.declaredRecordCount >= 0
        && document.declaredRecordCount != document.rows.size()) {
        document.warnings.append(QStringLiteral(
            "记录数不一致：文件头声明 %1 条，实际解析 %2 条")
            .arg(document.declaredRecordCount).arg(document.rows.size()));
    }

    // 无结束标识时，按配置丢弃文件尾部的行
    if (!document.sawEndMarker && definition.endIgnoreRows > 0) {
        const int drop = qMin(definition.endIgnoreRows, document.rows.size());
        document.rows.resize(document.rows.size() - drop);
        document.sourceLines.resize(document.sourceLines.size() - drop);
    }

    if (!document.sawEndMarker && !definition.lastRowCheck.isEmpty()) {
        document.warnings.append(QStringLiteral(
            "未找到文件结束标识 %1，文件可能不完整").arg(definition.lastRowCheck));
    }

    if (document.rows.isEmpty()) {
        document.warnings.append(QStringLiteral("文件中没有解析到数据行"));
    }

    document.parseMilliseconds = static_cast<int>(timer.elapsed());
    return true;
}

bool rfv::FileParser::parseDelimited(const QString& path, Document& document, QString* error)
{
    QElapsedTimer timer;
    timer.start();

    const QByteArray raw = readAllBytes(path);
    if (raw.isEmpty()) {
        if (error)
            *error = QStringLiteral("文件为空：%1").arg(path);
        return false;
    }

    QByteArray data = raw;
    const bool hasBom = data.startsWith("\xEF\xBB\xBF");
    if (hasBom)
        data.remove(0, 3);

    const QString encoding = detectTextEncoding(data);
    const QString text = decodeText(data, encoding);

    Document out;
    out.filePath = path;
    out.hasBom = hasBom;
    out.endsWithNewline = text.endsWith(QLatin1Char('\n'));

    QString lineEnding = QStringLiteral("\r\n");
    if (text.contains(QStringLiteral("\r\n")))
        lineEnding = QStringLiteral("\r\n");
    else if (text.contains(QLatin1Char('\n')))
        lineEnding = QStringLiteral("\n");
    out.lineEnding = lineEnding;

    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
    }
    if (out.endsWithNewline && !lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QStringList headerFields;
    const int headerIndex = findDelimitedHeader(lines, &headerFields);
    if (headerIndex < 0) {
        if (error) {
            *error = QStringLiteral("无法识别分隔符文件的表头（需要“元数据行 + 竖线表头 + 数据行”结构）");
        }
        return false;
    }

    InterfaceDefinition definition;
    definition.layout = Layout::Delimited;
    definition.configFile = QFileInfo(path).fileName();
    definition.section = definition.configFile;
    definition.protocol = QStringLiteral("内部清算文件");
    definition.encoding = encoding;
    definition.delimiter = QStringLiteral("|");
    definition.fieldCount = headerFields.size();
    definition.dataBeginRow = headerIndex + 2;
    definition.describe = QFileInfo(path).completeBaseName();
    definition.rowLength = 0;
    definition.lengthType = 0;

    for (int i = 0; i < headerFields.size(); ++i) {
        FieldDefinition field;
        field.index = i + 1;
        field.nameCn = headerFields.at(i).trimmed();
        if (field.nameCn.isEmpty())
            field.nameCn = QStringLiteral("列%1").arg(i + 1);
        field.nameEn = field.nameCn;
        field.type = QLatin1Char('C');
        field.length = 0;
        field.decimals = 0;
        field.offset = 0;
        definition.fields.append(field);
    }

    // 文件名：机构_类型_业务_日期.txt
    const QStringList parts = QFileInfo(path).completeBaseName().split(QLatin1Char('_'));
    if (parts.size() >= 4) {
        out.senderCode = parts.at(0);
        definition.fileType = parts.at(1);
        definition.describe = QStringLiteral("%1 内部清算文件").arg(parts.at(2));
        if (parts.at(3).size() == 8)
            out.fileDate = parts.at(3);
    }

    out.definition = definition;
    out.preambleLines = lines.mid(0, headerIndex);
    out.headerRaw = lines.at(headerIndex);
    out.headerLines = out.preambleLines;
    out.headerLines.append(out.headerRaw);

    const int fieldCount = headerFields.size();
    for (int i = headerIndex + 1; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        if (line.isEmpty())
            continue;

        QStringList fields = line.split(QLatin1Char('|'));
        if (fields.size() < fieldCount) {
            // 字段数不足的行按文件尾保留，避免丢失汇总行
            out.tailLines.append(line);
            continue;
        }
        if (fields.size() > fieldCount)
            fields = fields.mid(0, fieldCount);

        out.values.append(fields);
        out.sourceLines.append(i + 1);
    }

    if (out.values.isEmpty())
        out.warnings.append(QStringLiteral("文件中没有解析到数据行"));

    out.declaredRecordCount = -1;
    out.sawEndMarker = false;
    out.parseMilliseconds = static_cast<int>(timer.elapsed());

    document = out;
    return true;
}

QString rfv::Document::value(int row, int fieldIndex) const
{
    if (isDelimited()) {
        if (row < 0 || row >= values.size())
            return QString();
        if (fieldIndex < 0 || fieldIndex >= values.at(row).size())
            return QString();
        return values.at(row).at(fieldIndex);
    }

    if (row < 0 || row >= rows.size())
        return QString();
    if (fieldIndex < 0 || fieldIndex >= definition.fields.size())
        return QString();

    const FieldDefinition& field = definition.fields.at(fieldIndex);
    QString text;
    if (definition.lengthType == 1) {
        const QString line = decodeText(rows.at(row), definition.encoding);
        text = line.mid(field.offset, field.length);
    } else {
        text = decodeText(rows.at(row).mid(field.offset, field.length),
                          definition.encoding);
    }
    return formatFieldValue(text, field);
}

QStringList rfv::Document::rowValues(int row) const
{
    QStringList values;
    values.reserve(definition.fields.size());
    for (int i = 0; i < definition.fields.size(); ++i)
        values.append(value(row, i));
    return values;
}

QString rfv::Document::sourceLineText(int row) const
{
    if (isDelimited()) {
        if (row < 0 || row >= values.size())
            return QString();
        return values.at(row).join(definition.delimiter);
    }

    if (row < 0 || row >= rows.size())
        return QString();
    return decodeText(rows.at(row), definition.encoding);
}
