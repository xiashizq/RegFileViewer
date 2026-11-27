#include "core/tablemodel.h"

#include "core/encoding.h"

#include <QColor>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <QPair>
#include <algorithm>
#include <utility>

namespace rfv {

namespace {

QString numericFieldText(const FieldDefinition& field, const QString& value,
                         bool* ok, QString* error)
{
    *ok = true;
    const QString text = value.trimmed();
    if (text.isEmpty())
        return QString(field.length, QLatin1Char(' '));

    const QRegularExpression pattern(QStringLiteral("^-?\\d+(?:\\.\\d+)?$"));
    if (!pattern.match(text).hasMatch()) {
        *ok = false;
        if (error) *error = QStringLiteral("字段“%1”不是有效数值：%2").arg(field.nameCn, text);
        return QString();
    }

    QString body = text;
    bool negative = false;
    if (body.startsWith(QLatin1Char('-'))) {
        negative = true;
        body.remove(0, 1);
    }

    QString integerPart = body;
    QString fractionPart;
    const int dot = body.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        integerPart = body.left(dot);
        fractionPart = body.mid(dot + 1);
    }

    if (field.decimals <= 0) {
        if (!fractionPart.isEmpty()) {
            *ok = false;
            if (error) *error = QStringLiteral("字段“%1”不允许小数").arg(field.nameCn);
            return QString();
        }
    } else {
        if (fractionPart.size() > field.decimals) {
            *ok = false;
            if (error) *error = QStringLiteral("字段“%1”小数位超过 %2 位")
                    .arg(field.nameCn).arg(field.decimals);
            return QString();
        }
        fractionPart = fractionPart.leftJustified(field.decimals, QLatin1Char('0'));
    }

    while (integerPart.size() > 1 && integerPart.startsWith(QLatin1Char('0')))
        integerPart.remove(0, 1);
    if (integerPart.isEmpty())
        integerPart = QStringLiteral("0");

    const QString digits = integerPart + fractionPart;
    const int signWidth = negative ? 1 : 0;
    if (digits.size() + signWidth > field.length) {
        *ok = false;
        if (error) *error = QStringLiteral("字段“%1”的数值超出 %2 位字段长度")
                .arg(field.nameCn).arg(field.length);
        return QString();
    }

    return (negative ? QStringLiteral("-") : QString())
            + QString(field.length - digits.size() - signWidth, QLatin1Char('0'))
            + digits;
}

QString paddedFieldText(const FieldDefinition& field, const QString& value)
{
    QString text = value.trimmed();
    if (text.size() > field.length)
        text.truncate(field.length);
    return text.leftJustified(field.length, QLatin1Char(' '));
}

QByteArray paddedFieldBytes(const FieldDefinition& field, const QString& value,
                            const QString& encoding, bool* ok, QString* error)
{
    if (field.type == QLatin1Char('N'))
        return numericFieldText(field, value, ok, error).toLatin1();

    *ok = true;
    const QString text = value.trimmed();
    QByteArray encoded;
    for (const QChar ch : text) {
        const QByteArray piece = encodeText(QString(ch), encoding);
        if (encoded.size() + piece.size() > field.length)
            break;
        encoded += piece;
    }
    return encoded.leftJustified(field.length, ' ');
}

struct RawLine
{
    QByteArray content;
    QByteArray ending;
};

QVector<RawLine> splitLinesWithEndings(const QByteArray& data)
{
    QVector<RawLine> lines;
    int start = 0;
    while (start < data.size()) {
        const int newline = data.indexOf('\n', start);
        if (newline < 0) {
            lines.append({data.mid(start), QByteArray()});
            break;
        }

        QByteArray content = data.mid(start, newline - start);
        QByteArray ending("\n");
        if (content.endsWith('\r')) {
            content.chop(1);
            ending = "\r\n";
        }
        lines.append({content, ending});
        start = newline + 1;
    }
    return lines;
}

QString formatRecordCount(const InterfaceDefinition& definition,
                          const QByteArray& original, int count)
{
    const QString number = QString::number(count);

    if (definition.isOfd())
        return number.rightJustified(definition.rowCountLength, QLatin1Char('0'));

    const QByteArray trimmed = original.trimmed();
    bool allDigits = !trimmed.isEmpty();
    for (char ch : trimmed) {
        if (ch < '0' || ch > '9') {
            allDigits = false;
            break;
        }
    }
    if (allDigits)
        return number.rightJustified(trimmed.size(), QLatin1Char('0'));
    return number;
}

/// 保留原行的前导/尾随空白，只按原数字宽度补零，用于 OFD 字段名后的记录数行。
QString formatRecordCountPreservingPadding(const QString& original, int count)
{
    const QString trimmed = original.trimmed();
    if (trimmed.isEmpty())
        return QString::number(count);

    for (const QChar ch : trimmed) {
        if (!ch.isDigit())
            return QString::number(count);
    }

    const int left = original.indexOf(trimmed);
    return original.left(left)
            + QString::number(count).rightJustified(trimmed.size(), QLatin1Char('0'))
            + original.mid(left + trimmed.size());
}

/// 分隔符文件另存为后，文件名就是文件类型/发送方/日期等信息来源，需要同步更新。
/// 文件名不满足“机构_类型_业务_日期”四段结构时保留原值（可能来自用户编辑），不覆盖。
void updateDelimitedMetadata(Document& document, const QString& path)
{
    const QFileInfo info(path);
    document.filePath = info.absoluteFilePath();

    InterfaceDefinition& definition = document.definition;
    definition.configFile = info.fileName();
    definition.section = info.fileName();
    definition.describe = info.completeBaseName();

    const QStringList parts = info.completeBaseName().split(QLatin1Char('_'));
    if (parts.size() >= 4) {
        document.senderCode = parts.at(0);
        definition.fileType = parts.at(1);
        definition.describe = QStringLiteral("%1 内部清算文件").arg(parts.at(2));
        if (parts.at(3).size() == 8)
            document.fileDate = parts.at(3);
    }
}

/// 用文档中的全部数据行重建文件：报文头与文件尾原样保留，数据区按行顺序输出，
/// 同时更新记录数声明。firstDataLine 返回第一条数据行的行号（1 开始）。
bool rebuildFile(const QString& sourcePath, const QString& targetPath,
                 const QVector<QByteArray>& dataRows,
                 const InterfaceDefinition& definition,
                 const QHash<int, QByteArray>* headerOverrides,
                 int* firstDataLine, QString* error)
{
    QFile input(sourcePath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取源文件：%1").arg(sourcePath);
        return false;
    }
    QByteArray data = input.readAll();
    input.close();

    QByteArray bom;
    if (data.startsWith("\xEF\xBB\xBF")) {
        bom = data.left(3);
        data.remove(0, 3);
    }

    QVector<RawLine> lines = splitLinesWithEndings(data);
    const int headerCount = definition.dataBeginRow - 1;
    if (headerCount < 0 || headerCount > lines.size()) {
        if (error)
            *error = QStringLiteral("无法定位数据区：文件头行数不足");
        return false;
    }

    // 文件尾：从末尾向前找结束标识行
    int tailStart = lines.size();
    if (!definition.lastRowCheck.isEmpty()) {
        for (int i = lines.size() - 1; i >= headerCount; --i) {
            if (QString::fromLatin1(lines.at(i).content).trimmed()
                    .compare(definition.lastRowCheck, Qt::CaseInsensitive) == 0) {
                tailStart = i;
                break;
            }
        }
    }

    const QByteArray dataEnding = headerCount > 0 && !lines.at(headerCount - 1).ending.isEmpty()
            ? lines.at(headerCount - 1).ending
            : QByteArray("\r\n");

    // 记录数所在行
    const int recordLine = (definition.isOfd() ? 6 : definition.rowCountCheckRow) - 1;
    if (recordLine >= 0 && recordLine < headerCount) {
        lines[recordLine].content = formatRecordCount(
                    definition, lines.at(recordLine).content, dataRows.size()).toLatin1();
    }

    // OFD 字段名后、数据前可能还有一行记录数（例如 00035397），同样同步更新
    const int dataRecordLine = definition.dataRecordCountRow - 1;
    if (definition.isOfd() && dataRecordLine >= 0 && dataRecordLine < headerCount
        && dataRecordLine != recordLine) {
        const QString original = QString::fromLatin1(lines.at(dataRecordLine).content);
        lines[dataRecordLine].content =
                formatRecordCountPreservingPadding(original, dataRows.size()).toLatin1();
    }

    QByteArray output = bom;
    for (int i = 0; i < headerCount; ++i) {
        // 报文头行默认取自源文件；被修改过的行（如发送方/接收方/文件日期）用编辑后的内容
        QByteArray content = lines.at(i).content;
        if (headerOverrides && headerOverrides->contains(i))
            content = headerOverrides->value(i);
        output += content;
        output += lines.at(i).ending.isEmpty() ? dataEnding : lines.at(i).ending;
    }

    for (const QByteArray& row : dataRows) {
        output += row;
        output += dataEnding;
    }

    for (int i = tailStart; i < lines.size(); ++i) {
        output += lines.at(i).content;
        output += lines.at(i).ending;
    }

    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(targetPath);
        return false;
    }
    if (file.write(output) != output.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("保存文件失败：%1").arg(targetPath);
        return false;
    }

    if (firstDataLine)
        *firstDataLine = definition.dataBeginRow;
    return true;
}

} // namespace

TableModel::TableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

quint64 TableModel::editKey(int documentRow, int dataColumn)
{
    return (quint64(quint32(documentRow)) << 32) | quint32(dataColumn);
}

void TableModel::setDocument(Document document)
{
    beginResetModel();
    m_document = std::move(document);
    m_hasDocument = true;
    m_visible.clear();
    m_extraNames.clear();
    m_extraValues.clear();
    m_edits.clear();
    m_hasRemovedRows = false;
    m_metaChanged = false;
    m_columnFilters.clear();
    endResetModel();
    rebuildVisibleRows();
}

void TableModel::clear()
{
    beginResetModel();
    m_document = Document();
    m_hasDocument = false;
    m_visible.clear();
    m_extraNames.clear();
    m_extraValues.clear();
    m_edits.clear();
    m_hasRemovedRows = false;
    m_metaChanged = false;
    m_columnFilters.clear();
    endResetModel();
}

void TableModel::setHeaderMetadata(const QString& sender, const QString& receiver,
                                   const QString& date)
{
    if (!m_hasDocument)
        return;

    Document& doc = m_document;
    const bool changed = doc.senderCode != sender || doc.receiverCode != receiver
            || doc.fileDate != date;
    if (!changed)
        return;

    doc.senderCode = sender;
    doc.receiverCode = receiver;
    doc.fileDate = date;
    m_metaChanged = true;

    // OFD 报文头第 3/4/5 行是发送方/接收方/文件日期，
    // 修改后立即同步到内存文件头，保存时写入文件并重命名为新元数据对应的文件名。
    // 短于原行的值按原行宽补空格，保持报文头列宽不变。
    if (doc.definition.layout == Layout::Ofd && doc.headerLines.size() >= 5) {
        auto writeHeaderLine = [&doc](int row, const QString& value) {
            QString text = value.trimmed();
            const int originalWidth = doc.headerLines.at(row).size();
            if (text.size() < originalWidth)
                text = text.leftJustified(originalWidth, QLatin1Char(' '));
            doc.headerLines[row] = text;
        };
        writeHeaderLine(2, sender);
        writeHeaderLine(3, receiver);
        writeHeaderLine(4, date);
    }
}

/// 行数变化后，立即把记录数统计行的值同步到内存文件头，
/// “文件头”页与“数据概况”的“声明 X”即时刷新；保存时再写回文件。
void TableModel::updateCountLines()
{
    if (!m_hasDocument || m_document.isDelimited())
        return;

    QStringList& header = m_document.headerLines;
    if (header.isEmpty())
        return;

    const InterfaceDefinition& definition = m_document.definition;
    const int rowCount = m_document.rowCount();
    bool updated = false;

    // OFD 报文头第 6 行；定长文件为 rowcountcheckrow 指定的
    // “字段名之后、数据之前”的统计行
    const int recordRow = definition.isOfd() ? 6 : definition.rowCountCheckRow;
    if (recordRow > 0 && recordRow <= header.size()) {
        header[recordRow - 1] = formatRecordCount(
                    definition, header.at(recordRow - 1).toLatin1(), rowCount);
        updated = true;
    }

    // OFD 字段名之后、数据之前的记录数行（例如 00035397）
    const int dataRecordRow = definition.dataRecordCountRow;
    if (definition.isOfd() && dataRecordRow > 0 && dataRecordRow != recordRow
        && dataRecordRow <= header.size()) {
        header[dataRecordRow - 1] = formatRecordCountPreservingPadding(
                    header.at(dataRecordRow - 1), rowCount);
        updated = true;
    }

    if (updated)
        m_document.declaredRecordCount = rowCount;
}

/// 元数据被修改后，按当前发送方/接收方/文件日期推导保存时应使用的文件名。
/// 文件名需符合“发送方-接收方-类型-日期”（OFD/定长）或
/// “机构_类型_业务_日期”（内部清算）的四段结构，识别不了时返回空表示不改名。
QString TableModel::metadataTargetPath(const QString& path) const
{
    const QFileInfo info(path);
    const bool delimited = m_document.isDelimited();
    const QChar separator = delimited ? QLatin1Char('_') : QLatin1Char('-');

    QStringList parts = info.completeBaseName().split(separator);
    if (parts.size() < 4)
        return QString();

    // 新值里不能出现文件名分隔符，否则文件名结构会被破坏
    const auto containsSeparator = [&separator](const QString& value) {
        return value.contains(separator) || value.contains(QLatin1Char('_'))
                || value.contains(QLatin1Char('-'));
    };
    if (containsSeparator(m_document.senderCode)
        || (!delimited && containsSeparator(m_document.receiverCode))
        || containsSeparator(m_document.fileDate))
        return QString();

    if (!m_document.senderCode.isEmpty())
        parts[0] = m_document.senderCode;
    if (!delimited && !m_document.receiverCode.isEmpty())
        parts[1] = m_document.receiverCode;
    if (m_document.fileDate.size() == 8)
        parts[3] = m_document.fileDate;

    const QString suffix = info.suffix().isEmpty()
            ? QString() : QStringLiteral(".") + info.suffix();
    return info.dir().filePath(parts.join(separator) + suffix);
}

/// 把刚写好的文件重命名为元数据对应的文件名。
/// 目标文件名已存在时保持原文件名，避免覆盖已有文件。
QString TableModel::renameForMetadata(const QString& writtenPath) const
{
    const QString target = metadataTargetPath(writtenPath);
    if (target.isEmpty() || target == writtenPath)
        return writtenPath;
    if (QFileInfo::exists(target))
        return writtenPath;
    return QFile::rename(writtenPath, target) ? target : writtenPath;
}


int TableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_visible.size();
}

int TableModel::dataColumnCount() const
{
    if (!m_hasDocument)
        return 0;
    return m_document.definition.fields.size() + m_extraNames.size();
}

int TableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_hasDocument)
        return 0;
    return dataColumnCount() + 1;
}

QString TableModel::fieldHeaderText(const FieldDefinition& field) const
{
    if (m_showEnglish) {
        if (!field.nameEn.isEmpty() && !field.nameCn.isEmpty())
            return QStringLiteral("%1 / %2").arg(field.nameCn, field.nameEn);
        return field.nameEn.isEmpty() ? field.nameCn : field.nameEn;
    }
    return field.nameCn.isEmpty() ? field.nameEn : field.nameCn;
}

QString TableModel::dataColumnHeader(int dataColumn) const
{
    if (!m_hasDocument)
        return QString();
    const int fieldCount = m_document.definition.fields.size();
    if (dataColumn < fieldCount)
        return fieldHeaderText(m_document.definition.fields.at(dataColumn));
    const int extra = dataColumn - fieldCount;
    if (extra >= 0 && extra < m_extraNames.size())
        return m_extraNames.at(extra);
    return QString();
}

bool TableModel::isExtraColumn(int dataColumn) const
{
    return m_hasDocument && dataColumn >= m_document.definition.fields.size();
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (!m_hasDocument)
        return QVariant();

    if (orientation == Qt::Vertical) {
        if (role == Qt::DisplayRole)
            return section + 1;
        return QVariant();
    }

    if (role == Qt::TextAlignmentRole) {
        // 序号列表头居中显示
        if (section == 0)
            return static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter);
        return QVariant();
    }

    if (role == Qt::DisplayRole) {
        if (section == 0)
            return QStringLiteral("序号");
        const int dataColumn = section - 1;
        if (dataColumn < 0 || dataColumn >= dataColumnCount())
            return QVariant();
        return dataColumnHeader(dataColumn);
    }

    if (role == Qt::ToolTipRole) {
        if (section == 0)
            return QStringLiteral("列表序号（仅显示，不写入文件、不参与导出）");

        const int dataColumn = section - 1;
        if (dataColumn < 0 || dataColumn >= dataColumnCount())
            return QVariant();

        if (isExtraColumn(dataColumn))
            return QStringLiteral("用户新增列\n可在单元格中直接编辑，导出时一并输出");

        const FieldDefinition& field = m_document.definition.fields.at(dataColumn);
        QString tip = QStringLiteral("第 %1 个字段\n中文名：%2\n英文名：%3\n类型：%4  长度：%5  小数位：%6")
                .arg(field.index)
                .arg(field.nameCn, field.nameEn)
                .arg(field.type)
                .arg(field.length)
                .arg(field.decimals);
        if (!field.tip.isEmpty())
            tip += QStringLiteral("\n说明：") + field.tip;
        if (m_document.definition.dictionaries) {
            const auto& dictionaries = *m_document.definition.dictionaries;
            auto it = dictionaries.constFind(field.nameEn);
            if (it == dictionaries.constEnd())
                it = dictionaries.constFind(field.nameCn);
            if (it != dictionaries.constEnd())
                tip += QStringLiteral("\n字典项：%1 个").arg(it->size());
        }
        return tip;
    }

    return QVariant();
}

QString TableModel::cellValue(int documentRow, int dataColumn) const
{
    if (!m_hasDocument || documentRow < 0 || documentRow >= m_document.rowCount())
        return QString();
    if (dataColumn < 0 || dataColumn >= dataColumnCount())
        return QString();

    const int fieldCount = m_document.definition.fields.size();
    if (dataColumn < fieldCount) {
        const auto it = m_edits.constFind(editKey(documentRow, dataColumn));
        if (it != m_edits.constEnd())
            return it.value();
        return m_document.value(documentRow, dataColumn);
    }

    const int extra = dataColumn - fieldCount;
    return m_extraValues.at(extra).value(documentRow);
}

QString TableModel::translatedValue(int documentRow, int fieldIndex) const
{
    if (!m_hasDocument || !m_document.definition.dictionaries)
        return QString();
    if (fieldIndex < 0 || fieldIndex >= m_document.definition.fields.size())
        return QString();

    const FieldDefinition& field = m_document.definition.fields.at(fieldIndex);
    const auto& dictionaries = *m_document.definition.dictionaries;

    auto it = dictionaries.constFind(field.nameEn);
    if (it == dictionaries.constEnd())
        it = dictionaries.constFind(field.nameCn);
    if (it == dictionaries.constEnd())
        return QString();

    const QString raw = cellValue(documentRow, fieldIndex);
    if (raw.isEmpty())
        return QString();

    auto valueIt = it->constFind(raw);
    if (valueIt == it->constEnd())
        valueIt = it->constFind(raw.trimmed());
    return valueIt == it->constEnd() ? QString() : *valueIt;
}

QVariant TableModel::data(const QModelIndex& index, int role) const
{
    if (!m_hasDocument || !index.isValid())
        return QVariant();

    const int visibleRow = index.row();
    if (visibleRow < 0 || visibleRow >= m_visible.size())
        return QVariant();
    const int documentRow = m_visible.at(visibleRow);

    if (index.column() == 0) {
        if (role == Qt::DisplayRole)
            return visibleRow + 1;
        if (role == Qt::ToolTipRole)
            return QStringLiteral("列表序号：%1（仅显示，不写入文件、不参与导出）")
                    .arg(visibleRow + 1);
        if (role == Qt::TextAlignmentRole)
            return static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter);
        if (role == Qt::ForegroundRole)
            return QColor(120, 120, 120);
        return QVariant();
    }

    const int dataColumn = index.column() - 1;
    if (dataColumn < 0 || dataColumn >= dataColumnCount())
        return QVariant();

    const bool extra = isExtraColumn(dataColumn);
    const FieldDefinition* field = extra ? nullptr
                                         : &m_document.definition.fields.at(dataColumn);

    if (role == Qt::DisplayRole)
        return displayValue(documentRow, dataColumn);

    if (role == Qt::EditRole)
        return cellValue(documentRow, dataColumn);

    if (role == Qt::ToolTipRole) {
        const QString value = cellValue(documentRow, dataColumn);
        if (extra) {
            return QStringLiteral("%1\n值：%2\n源行号：%3\n（用户新增列，可直接编辑）")
                    .arg(dataColumnHeader(dataColumn), value)
                    .arg(m_document.sourceLines.value(documentRow));
        }

        QString tip = QStringLiteral("%1（%2）\n值：%3")
                .arg(field->nameCn, field->nameEn, value);
        const QString meaning = translatedValue(documentRow, dataColumn);
        if (!meaning.isEmpty())
            tip += QStringLiteral("\n含义：") + meaning;
        tip += QStringLiteral("\n源行号：%1").arg(m_document.sourceLines.value(documentRow));
        tip += QStringLiteral("\n（双击可编辑）");
        return tip;
    }

    if (role == Qt::TextAlignmentRole) {
        if (extra)
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        if (field->type == QLatin1Char('N') || field->type == QLatin1Char('A'))
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ForegroundRole) {
        if (cellValue(documentRow, dataColumn).isEmpty())
            return QColor(160, 160, 160);
        return QVariant();
    }

    return QVariant();
}

bool TableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!m_hasDocument || !index.isValid() || index.column() <= 0)
        return false;
    if (role != Qt::EditRole && role != Qt::DisplayRole)
        return false;

    const int dataColumn = index.column() - 1;
    if (dataColumn < 0 || dataColumn >= dataColumnCount())
        return false;

    const int visibleRow = index.row();
    if (visibleRow < 0 || visibleRow >= m_visible.size())
        return false;
    const int documentRow = m_visible.at(visibleRow);

    const QString newValue = value.toString();
    const int fieldCount = m_document.definition.fields.size();

    if (dataColumn < fieldCount) {
        const QString original = m_document.value(documentRow, dataColumn);
        if (newValue == original)
            m_edits.remove(editKey(documentRow, dataColumn));
        else
            m_edits.insert(editKey(documentRow, dataColumn), newValue);
    } else {
        const int extra = dataColumn - fieldCount;
        if (newValue.isEmpty())
            m_extraValues[extra].remove(documentRow);
        else
            m_extraValues[extra].insert(documentRow, newValue);
    }

    emit dataChanged(index, index,
                     QVector<int>() << Qt::DisplayRole << Qt::EditRole
                                    << Qt::ToolTipRole << Qt::ForegroundRole);
    return true;
}

Qt::ItemFlags TableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags base = QAbstractTableModel::flags(index);
    if (m_hasDocument && index.isValid() && index.column() > 0)
        base |= Qt::ItemIsEditable;
    return base;
}

bool TableModel::appendEmptyColumn(QString* columnName)
{
    if (!m_hasDocument)
        return false;

    const int insertAt = columnCount();
    beginInsertColumns(QModelIndex(), insertAt, insertAt);
    m_extraNames.append(QStringLiteral("新增列%1").arg(m_extraNames.size() + 1));
    m_extraValues.append(QHash<int, QString>());
    endInsertColumns();

    if (columnName)
        *columnName = m_extraNames.last();
    return true;
}

bool TableModel::duplicateColumn(int dataColumn, QString* columnName)
{
    if (!m_hasDocument || dataColumn < 0 || dataColumn >= dataColumnCount())
        return false;

    const int sourceColumn = dataColumn;

    QHash<int, QString> values;
    values.reserve(m_document.rowCount());
    for (int row = 0; row < m_document.rowCount(); ++row) {
        const QString value = cellValue(row, sourceColumn);
        if (!value.isEmpty())
            values.insert(row, value);
    }

    QString base = dataColumnHeader(sourceColumn);
    if (base.isEmpty())
        base = QStringLiteral("列");
    QString name = QStringLiteral("%1 副本").arg(base);
    const QString first = name;
    int suffix = 2;
    while ([this, &name]() {
               for (int i = 0; i < m_extraNames.size(); ++i) {
                   if (m_extraNames.at(i) == name)
                       return true;
               }
               return false;
           }()) {
        name = QStringLiteral("%1 %2").arg(first).arg(suffix++);
    }

    const int insertAt = columnCount();
    beginInsertColumns(QModelIndex(), insertAt, insertAt);
    m_extraNames.append(name);
    m_extraValues.append(values);
    endInsertColumns();

    if (columnName)
        *columnName = name;
    return true;
}

bool TableModel::renameColumn(int dataColumn, const QString& name)
{
    if (!m_hasDocument || dataColumn < 0 || name.trimmed().isEmpty())
        return false;

    if (!isExtraColumn(dataColumn))
        return false;

    const int extra = dataColumn - m_document.definition.fields.size();
    if (extra < 0 || extra >= m_extraNames.size())
        return false;

    m_extraNames[extra] = name.trimmed();
    emit headerDataChanged(Qt::Horizontal, dataColumn + 1, dataColumn + 1);
    return true;
}

int TableModel::modifiedCellCount() const
{
    int count = m_edits.size();
    for (const auto& values : m_extraValues)
        count += values.size();
    return count;
}

void TableModel::setTranslateDictionary(bool enabled)
{
    if (m_translate == enabled)
        return;
    m_translate = enabled;
    if (!m_visible.isEmpty() && rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         QVector<int>() << Qt::DisplayRole << Qt::ToolTipRole);
    }
}

/// 该行是否同时满足所有非空的列筛选条件（与全文筛选叠加）
bool TableModel::columnsMatched(int documentRow) const
{
    if (!m_hasDocument)
        return true;

    const int columns = qMin(dataColumnCount(), m_columnFilters.size());
    for (int column = 0; column < columns; ++column) {
        const ColumnFilter& filter = m_columnFilters.at(column);
        if (filter.text.isEmpty() && !filter.filterByValues)
            continue;

        const QString display = displayValue(documentRow, column);
        if (!filter.text.isEmpty()
            && !display.contains(filter.text, Qt::CaseInsensitive))
            return false;
        if (filter.filterByValues && !filter.values.contains(display))
            return false;
    }
    return true;
}

void TableModel::setShowEnglishHeader(bool enabled)
{
    if (m_showEnglish == enabled)
        return;
    m_showEnglish = enabled;
    if (m_hasDocument)
        emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
}

void TableModel::setFilter(const QString& text, bool regularExpression)
{
    m_filterText = text;
    m_filterRegex = regularExpression;
    rebuildVisibleRows();
}

void TableModel::setColumnFilter(int dataColumn, const ColumnFilter& filter)
{
    if (!m_hasDocument || dataColumn < 0 || dataColumn >= dataColumnCount())
        return;

    if (dataColumn >= m_columnFilters.size())
        m_columnFilters.resize(dataColumnCount());

    ColumnFilter normalized = filter;
    normalized.text = filter.text.trimmed();

    if (m_columnFilters.at(dataColumn).text == normalized.text
        && m_columnFilters.at(dataColumn).filterByValues == normalized.filterByValues
        && m_columnFilters.at(dataColumn).values == normalized.values)
        return;

    m_columnFilters[dataColumn] = normalized;
    rebuildVisibleRows();
}

void TableModel::clearColumnFilters()
{
    if (m_columnFilters.isEmpty())
        return;
    m_columnFilters.clear();
    rebuildVisibleRows();
}

ColumnFilter TableModel::columnFilter(int dataColumn) const
{
    return m_columnFilters.value(dataColumn);
}

bool TableModel::hasColumnFilters() const
{
    return std::any_of(m_columnFilters.cbegin(), m_columnFilters.cend(),
                       [](const ColumnFilter& filter) {
        return !filter.text.isEmpty() || filter.filterByValues;
    });
}

QStringList TableModel::distinctDisplayValues(int dataColumn) const
{
    if (!m_hasDocument || dataColumn < 0 || dataColumn >= dataColumnCount())
        return QStringList();

    QSet<QString> values;
    for (int row = 0; row < m_document.rowCount(); ++row)
        values.insert(displayValue(row, dataColumn));

    QStringList list = QStringList::fromSet(values);
    std::sort(list.begin(), list.end());
    return list;
}

QString TableModel::displayValue(int documentRow, int dataColumn) const
{
    if (!m_hasDocument || documentRow < 0 || documentRow >= m_document.rowCount()
        || dataColumn < 0 || dataColumn >= dataColumnCount())
        return QString();

    const QString value = cellValue(documentRow, dataColumn);
    if (isExtraColumn(dataColumn) || !m_translate)
        return value;

    const QString meaning = translatedValue(documentRow, dataColumn);
    if (meaning.isEmpty() || meaning == value)
        return value;
    return QStringLiteral("%1（%2）").arg(value, meaning);
}

void TableModel::rebuildVisibleRows()
{
    QVector<int> visible;
    if (!m_hasDocument) {
        beginResetModel();
        m_visible.clear();
        endResetModel();
        return;
    }

    const int totalRows = m_document.rowCount();
    const int columns = dataColumnCount();
    const QString filter = m_filterText.trimmed();

    QRegularExpression regex;
    bool useRegex = false;
    if (!filter.isEmpty() && m_filterRegex) {
        regex.setPattern(filter);
        regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        useRegex = regex.isValid();
    }

    visible.reserve(totalRows);
    for (int row = 0; row < totalRows; ++row) {
        bool matched = true;

        if (!filter.isEmpty()) {
            matched = false;
            if (useRegex) {
                const QString line = m_document.sourceLineText(row);
                matched = regex.match(line).hasMatch();
                if (!matched) {
                    for (int column = 0; column < columns && !matched; ++column) {
                        matched = regex.match(cellValue(row, column)).hasMatch();
                        if (!matched && m_translate) {
                            const QString meaning = translatedValue(row, column);
                            matched = !meaning.isEmpty() && regex.match(meaning).hasMatch();
                        }
                    }
                }
            } else {
                for (int column = 0; column < columns; ++column) {
                    if (cellValue(row, column).contains(filter, Qt::CaseInsensitive)) {
                        matched = true;
                        break;
                    }
                    if (m_translate) {
                        const QString meaning = translatedValue(row, column);
                        if (!meaning.isEmpty()
                            && meaning.contains(filter, Qt::CaseInsensitive)) {
                            matched = true;
                            break;
                        }
                    }
                }
            }
        }

        // 列筛选（表头输入框）：各列条件之间取与，与全文筛选叠加
        if (matched)
            matched = columnsMatched(row);

        if (matched)
            visible.append(row);
    }

    beginResetModel();
    m_visible = std::move(visible);
    endResetModel();
}

bool TableModel::applyFieldEdit(QByteArray& row, int fieldIndex, const QString& value,
                                QString* error) const
{
    const InterfaceDefinition& definition = m_document.definition;
    const FieldDefinition& field = definition.fields.at(fieldIndex);

    if (definition.lengthType == 0) {
        bool ok = true;
        const QByteArray bytes = paddedFieldBytes(field, value, definition.encoding,
                                                  &ok, error);
        if (!ok)
            return false;

        const int required = field.offset + field.length;
        if (row.size() < required)
            row.append(QByteArray(required - row.size(), ' '));
        row.replace(field.offset, field.length, bytes);
    } else {
        QString text;
        if (field.type == QLatin1Char('N')) {
            bool ok = true;
            text = numericFieldText(field, value, &ok, error);
            if (!ok)
                return false;
        } else {
            text = paddedFieldText(field, value);
        }

        QString line = decodeText(row, definition.encoding);
        const int required = field.offset + field.length;
        if (line.size() < required)
            line = line.leftJustified(required, QLatin1Char(' '));
        line.replace(field.offset, field.length, text);
        row = encodeText(line, definition.encoding);
    }
    return true;
}

bool TableModel::hasExtraColumnValues() const
{
    for (const QHash<int, QString>& values : m_extraValues) {
        if (!values.isEmpty())
            return true;
    }
    return false;
}

bool TableModel::hasUnsavedChanges() const
{
    if (!m_hasDocument)
        return false;
    return !m_edits.isEmpty() || addedRowCount() > 0 || m_hasRemovedRows
            || m_metaChanged;
}

int TableModel::addedRowCount() const
{
    if (!m_hasDocument)
        return 0;

    int count = 0;
    for (int line : m_document.sourceLines) {
        if (line < 0)
            ++count;
    }
    return count;
}

bool TableModel::saveToFile(const QString& path, QString* error, int* droppedBlankRows)
{
    if (!m_hasDocument) {
        if (error)
            *error = QStringLiteral("没有打开的文件");
        return false;
    }

    if (m_document.definition.layout == Layout::Delimited)
        return saveDelimitedFile(path, error, droppedBlankRows);

    QVector<QByteArray> dataRows = m_document.rows;

    // 按行合并字段编辑
    QHash<int, QVector<QPair<int, QString>>> editsByRow;
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        const int documentRow = int(quint32(it.key() >> 32));
        const int fieldIndex = int(it.key() & 0xffffffffu);
        if (documentRow < 0 || documentRow >= dataRows.size())
            continue;
        if (fieldIndex < 0 || fieldIndex >= m_document.definition.fields.size())
            continue;
        editsByRow[documentRow].append(qMakePair(fieldIndex, it.value()));
    }

    for (auto rowIt = editsByRow.constBegin(); rowIt != editsByRow.constEnd(); ++rowIt) {
        for (const QPair<int, QString>& edit : rowIt.value()) {
            if (!applyFieldEdit(dataRows[rowIt.key()], edit.first, edit.second, error))
                return false;
        }
    }

    // 完全空白的新增行没有数据含义（重新解析时会被当作空行跳过），
    // 保存时不写入文件，并从模型中移除。
    QVector<QByteArray> keptRows;
    QVector<int> keptOldIndices;
    keptRows.reserve(dataRows.size());
    keptOldIndices.reserve(dataRows.size());
    int dropped = 0;

    for (int i = 0; i < dataRows.size(); ++i) {
        const bool isNewRow = m_document.sourceLines.value(i, -1) < 0;
        if (isNewRow && dataRows.at(i).trimmed().isEmpty()) {
            ++dropped;
            continue;
        }
        keptRows.append(dataRows.at(i));
        keptOldIndices.append(i);
    }
    dataRows = keptRows;

    int firstDataLine = 0;

    // OFD 报文头第 3/4/5 行（发送方/接收方/文件日期）被修改过时写入保存的文件
    QHash<int, QByteArray> headerOverrides;
    if (m_document.definition.isOfd() && m_metaChanged
        && m_document.headerLines.size() >= 5) {
        for (int row : {2, 3, 4})
            headerOverrides.insert(row, encodeText(m_document.headerLines.at(row),
                                                   m_document.definition.encoding));
    }

    if (!rebuildFile(m_document.filePath, path, dataRows, m_document.definition,
                     &headerOverrides, &firstDataLine, error)) {
        return false;
    }

    // 行号可能因丢弃空白行而变化，重新建立映射
    QHash<int, int> rowMap;
    for (int newIndex = 0; newIndex < keptOldIndices.size(); ++newIndex)
        rowMap.insert(keptOldIndices.at(newIndex), newIndex);

    for (QHash<int, QString>& values : m_extraValues) {
        QHash<int, QString> remapped;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (rowMap.contains(it.key()))
                remapped.insert(rowMap.value(it.key()), it.value());
        }
        values = remapped;
    }

    m_document.rows = dataRows;
    m_document.sourceLines.clear();
    m_document.sourceLines.reserve(dataRows.size());
    for (int i = 0; i < dataRows.size(); ++i)
        m_document.sourceLines.append(firstDataLine + i);

    m_document.filePath = QFileInfo(path).absoluteFilePath();

    // 修改过发送方/接收方/文件日期时，按新元数据同步重命名文件
    if (m_metaChanged)
        m_document.filePath = renameForMetadata(m_document.filePath);

    // 文件中的记录数已经更新，同步内存中的声明值和文件头显示
    bool hasCountLine = false;
    const int recordLine = (m_document.definition.isOfd()
                            ? 6 : m_document.definition.rowCountCheckRow) - 1;
    if (recordLine >= 0 && recordLine < m_document.headerLines.size()) {
        const QByteArray original = m_document.headerLines.at(recordLine).toLatin1();
        m_document.headerLines[recordLine] =
                formatRecordCount(m_document.definition, original, dataRows.size());
        hasCountLine = true;
    }

    const int dataRecordLine = m_document.definition.dataRecordCountRow - 1;
    if (dataRecordLine >= 0 && dataRecordLine < m_document.headerLines.size()
        && dataRecordLine != recordLine) {
        m_document.headerLines[dataRecordLine] = formatRecordCountPreservingPadding(
                    m_document.headerLines.at(dataRecordLine), dataRows.size());
        hasCountLine = true;
    }
    m_document.declaredRecordCount = hasCountLine
            ? dataRows.size() : -1;

    // 保存后，记录数/空文件相关提示已经过时
    QStringList warnings;
    warnings.reserve(m_document.warnings.size());
    for (const QString& warning : m_document.warnings) {
        if (warning.startsWith(QStringLiteral("记录数不一致")))
            continue;
        if (!dataRows.isEmpty()
            && warning.startsWith(QStringLiteral("文件中没有解析到数据行"))) {
            continue;
        }
        warnings.append(warning);
    }
    m_document.warnings = warnings;

    m_edits.clear();
    m_hasRemovedRows = false;
    m_metaChanged = false;

    if (dropped > 0) {
        rebuildVisibleRows();
    } else if (!m_visible.isEmpty()) {
        emit dataChanged(index(0, 0),
                         index(m_visible.size() - 1, qMax(0, columnCount() - 1)),
                         QVector<int>() << Qt::DisplayRole << Qt::EditRole
                                        << Qt::ToolTipRole);
    }

    if (droppedBlankRows)
        *droppedBlankRows = dropped;
    return true;
}

bool TableModel::saveDelimitedFile(const QString& path, QString* error,
                                   int* droppedBlankRows)
{
    const InterfaceDefinition& definition = m_document.definition;
    const int fieldCount = definition.fieldCount;
    const QString delimiter = definition.delimiter.isEmpty()
            ? QStringLiteral("|") : definition.delimiter;

    QVector<QStringList> values = m_document.values;
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        const int row = int(quint32(it.key() >> 32));
        const int field = int(it.key() & 0xffffffffu);
        if (row >= 0 && row < values.size()
            && field >= 0 && field < values.at(row).size()) {
            values[row][field] = it.value();
        }
    }

    // 完全空白的新增行不写入文件
    QVector<QStringList> kept;
    QVector<int> keptOldIndices;
    kept.reserve(values.size());
    keptOldIndices.reserve(values.size());
    int dropped = 0;

    for (int i = 0; i < values.size(); ++i) {
        const QStringList& row = values.at(i);
        bool allEmpty = true;
        for (const QString& value : row) {
            if (!value.trimmed().isEmpty()) {
                allEmpty = false;
                break;
            }
        }
        if (m_document.sourceLines.value(i, -1) < 0 && allEmpty) {
            ++dropped;
            continue;
        }
        kept.append(row);
        keptOldIndices.append(i);
    }
    values = kept;

    // 字段中不能包含分隔符或换行
    for (int i = 0; i < values.size(); ++i) {
        const QStringList& row = values.at(i);
        for (int field = 0; field < row.size(); ++field) {
            const QString& value = row.at(field);
            if (value.contains(delimiter)
                || value.contains(QLatin1Char('\n'))
                || value.contains(QLatin1Char('\r'))) {
                if (error) {
                    const QString fieldName = field < definition.fields.size()
                            ? definition.fields.at(field).nameCn
                            : QStringLiteral("列%1").arg(field + 1);
                    *error = QStringLiteral("第 %1 行字段“%2”包含分隔符或换行，无法写入")
                            .arg(i + definition.dataBeginRow).arg(fieldName);
                }
                return false;
            }
        }
    }

    QString text;
    for (const QString& line : m_document.preambleLines)
        text += line + m_document.lineEnding;
    text += m_document.headerRaw + m_document.lineEnding;

    for (const QStringList& row : values) {
        QStringList padded = row;
        while (padded.size() < fieldCount)
            padded.append(QString());
        if (padded.size() > fieldCount)
            padded = padded.mid(0, fieldCount);
        text += padded.join(delimiter) + m_document.lineEnding;
    }

    for (const QString& line : m_document.tailLines)
        text += line + m_document.lineEnding;

    if (!m_document.endsWithNewline && text.endsWith(m_document.lineEnding))
        text.chop(m_document.lineEnding.size());

    QByteArray bytes = encodeText(text, definition.encoding);
    if (m_document.hasBom)
        bytes.prepend("\xEF\xBB\xBF");

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("保存文件失败：%1").arg(path);
        return false;
    }

    QHash<int, int> rowMap;
    for (int newIndex = 0; newIndex < keptOldIndices.size(); ++newIndex)
        rowMap.insert(keptOldIndices.at(newIndex), newIndex);

    for (QHash<int, QString>& extra : m_extraValues) {
        QHash<int, QString> remapped;
        for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
            if (rowMap.contains(it.key()))
                remapped.insert(rowMap.value(it.key()), it.value());
        }
        extra = remapped;
    }

    m_document.values = values;
    m_document.sourceLines.clear();
    m_document.sourceLines.reserve(values.size());
    for (int i = 0; i < values.size(); ++i)
        m_document.sourceLines.append(definition.dataBeginRow + i);

    // 修改过发送方/文件日期时，先把文件重命名为新元数据对应的文件名，
    // 再从最终文件名同步元数据，保证编辑值不丢失
    QString finalPath = QFileInfo(path).absoluteFilePath();
    if (m_metaChanged)
        finalPath = renameForMetadata(finalPath);
    updateDelimitedMetadata(m_document, finalPath);

    if (!values.isEmpty()) {
        QStringList warnings;
        warnings.reserve(m_document.warnings.size());
        for (const QString& warning : m_document.warnings) {
            if (warning.startsWith(QStringLiteral("文件中没有解析到数据行")))
                continue;
            warnings.append(warning);
        }
        m_document.warnings = warnings;
    }

    m_edits.clear();
    m_hasRemovedRows = false;
    m_metaChanged = false;

    if (dropped > 0) {
        rebuildVisibleRows();
    } else if (!m_visible.isEmpty()) {
        emit dataChanged(index(0, 0),
                         index(m_visible.size() - 1, qMax(0, columnCount() - 1)),
                         QVector<int>() << Qt::DisplayRole << Qt::EditRole
                                        << Qt::ToolTipRole);
    }

    if (droppedBlankRows)
        *droppedBlankRows = dropped;
    return true;
}

bool TableModel::appendBlankRow()
{
    if (!m_hasDocument || !m_filterText.isEmpty())
        return false;

    const bool delimited = m_document.definition.layout == Layout::Delimited;
    const int insertAt = m_document.rowCount();

    beginInsertRows(QModelIndex(), m_visible.size(), m_visible.size());
    if (delimited) {
        QStringList blank;
        blank.reserve(m_document.definition.fieldCount);
        for (int i = 0; i < m_document.definition.fieldCount; ++i)
            blank.append(QString());
        m_document.values.append(blank);
    } else {
        QByteArray blank;
        if (m_document.definition.lengthType == 0) {
            blank = QByteArray(m_document.definition.rowLength, ' ');
        } else {
            blank = encodeText(QString(m_document.definition.rowLength, QLatin1Char(' ')),
                               m_document.definition.encoding);
        }
        m_document.rows.append(blank);
    }
    m_document.sourceLines.append(-1);
    m_visible.append(insertAt);
    endInsertRows();
    updateCountLines();
    return true;
}

bool TableModel::duplicateRows(const QVector<int>& visibleRows)
{
    if (!m_hasDocument || visibleRows.isEmpty() || !m_filterText.isEmpty())
        return false;

    QVector<int> rows = visibleRows;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    QVector<int> sourceDocRows;
    sourceDocRows.reserve(rows.size());
    for (int visibleRow : rows) {
        if (visibleRow >= 0 && visibleRow < m_visible.size())
            sourceDocRows.append(m_visible.at(visibleRow));
    }
    if (sourceDocRows.isEmpty())
        return false;

    const int lastVisible = rows.last();
    if (lastVisible < 0 || lastVisible >= m_visible.size())
        return false;

    const int insertVisible = lastVisible + 1;
    const int insertDoc = m_visible.at(lastVisible) + 1;
    const int count = sourceDocRows.size();

    const bool delimited = m_document.definition.layout == Layout::Delimited;

    QVector<QByteArray> byteCopies;
    QVector<QStringList> valueCopies;
    QVector<QVector<QPair<int, QString>>> copyEdits;
    copyEdits.reserve(count);
    if (delimited)
        valueCopies.reserve(count);
    else
        byteCopies.reserve(count);

    for (int source : sourceDocRows) {
        if (delimited)
            valueCopies.append(m_document.values.at(source));
        else
            byteCopies.append(m_document.rows.at(source));

        QVector<QPair<int, QString>> edits;
        for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
            if (int(quint32(it.key() >> 32)) == source)
                edits.append(qMakePair(int(it.key() & 0xffffffffu), it.value()));
        }
        copyEdits.append(edits);
    }

    beginInsertRows(QModelIndex(), insertVisible, insertVisible + count - 1);

    for (int i = 0; i < count; ++i) {
        if (delimited)
            m_document.values.insert(insertDoc + i, valueCopies.at(i));
        else
            m_document.rows.insert(insertDoc + i, byteCopies.at(i));
        m_document.sourceLines.insert(insertDoc + i, -1);
    }
    shiftRowKeys(insertDoc, count);

    for (int i = 0; i < count; ++i) {
        const int destination = insertDoc + i;
        const int source = sourceDocRows.at(i);

        for (const QPair<int, QString>& edit : copyEdits.at(i))
            m_edits.insert(editKey(destination, edit.first), edit.second);

        for (int column = 0; column < m_extraValues.size(); ++column) {
            if (m_extraValues[column].contains(source))
                m_extraValues[column].insert(destination,
                                             m_extraValues[column].value(source));
        }
    }

    // 进入此函数前已确保没有筛选，可见行与文档行一一对应
    m_visible.clear();
    m_visible.reserve(m_document.rowCount());
    for (int i = 0; i < m_document.rowCount(); ++i)
        m_visible.append(i);

    endInsertRows();

    // 中间插入行后，后续行的“列表序号”需要重绘
    if (!m_visible.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_visible.size() - 1, 0),
                         QVector<int>() << Qt::DisplayRole);
    }
    updateCountLines();
    return true;
}

bool TableModel::deleteRows(const QVector<int>& visibleRows)
{
    if (!m_hasDocument || visibleRows.isEmpty())
        return false;

    QSet<int> deleted;
    for (int visibleRow : visibleRows) {
        if (visibleRow >= 0 && visibleRow < m_visible.size())
            deleted.insert(m_visible.at(visibleRow));
    }
    if (deleted.isEmpty())
        return false;

    const int oldCount = m_document.rowCount();
    QVector<QByteArray> newRows;
    QVector<QStringList> newValues;
    QVector<int> newSources;
    const int keptCount = oldCount - deleted.size();
    newRows.reserve(keptCount);
    newValues.reserve(keptCount);
    newSources.reserve(keptCount);

    QHash<int, int> remap;
    remap.reserve(keptCount);

    int nextRow = 0;
    for (int row = 0; row < oldCount; ++row) {
        if (deleted.contains(row))
            continue;

        if (m_document.isDelimited())
            newValues.append(m_document.values.at(row));
        else
            newRows.append(m_document.rows.at(row));
        newSources.append(m_document.sourceLines.value(row, -1));
        remap.insert(row, nextRow++);
    }

    beginResetModel();

    if (m_document.isDelimited())
        m_document.values = newValues;
    else
        m_document.rows = newRows;
    m_document.sourceLines = newSources;

    QHash<quint64, QString> edits;
    edits.reserve(m_edits.size());
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        const int oldRow = int(quint32(it.key() >> 32));
        const auto mapped = remap.constFind(oldRow);
        if (mapped != remap.constEnd()) {
            edits.insert(editKey(mapped.value(), int(it.key() & 0xffffffffu)),
                         it.value());
        }
    }
    m_edits = edits;

    for (QHash<int, QString>& values : m_extraValues) {
        QHash<int, QString> remapped;
        remapped.reserve(values.size());
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const auto mapped = remap.constFind(it.key());
            if (mapped != remap.constEnd())
                remapped.insert(mapped.value(), it.value());
        }
        values = remapped;
    }

    m_visible.clear();
    endResetModel();
    m_hasRemovedRows = true;
    rebuildVisibleRows();
    updateCountLines();
    return true;
}

void TableModel::shiftRowKeys(int insertAt, int count)
{
    QHash<quint64, QString> edits;
    edits.reserve(m_edits.size());
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        int row = int(quint32(it.key() >> 32));
        const int column = int(it.key() & 0xffffffffu);
        if (row >= insertAt)
            row += count;
        edits.insert(editKey(row, column), it.value());
    }
    m_edits = edits;

    for (QHash<int, QString>& values : m_extraValues) {
        QHash<int, QString> shifted;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const int row = it.key() >= insertAt ? it.key() + count : it.key();
            shifted.insert(row, it.value());
        }
        values = shifted;
    }
}

QByteArray TableModel::effectiveRowBytes(int documentRow) const
{
    if (!m_hasDocument || documentRow < 0 || documentRow >= m_document.rows.size())
        return QByteArray();

    QByteArray row = m_document.rows.at(documentRow);
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        if (int(quint32(it.key() >> 32)) != documentRow)
            continue;
        const int field = int(it.key() & 0xffffffffu);
        if (field >= 0 && field < m_document.definition.fields.size())
            applyFieldEdit(row, field, it.value(), nullptr);
    }
    return row;
}

QByteArray TableModel::rowsClipboardData(const QVector<int>& documentRows) const
{
    if (!m_hasDocument || documentRows.isEmpty())
        return QByteArray();

    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_15);

    const InterfaceDefinition& definition = m_document.definition;

    QStringList fieldNames;
    fieldNames.reserve(definition.fields.size());
    for (const FieldDefinition& field : definition.fields)
        fieldNames.append(field.nameCn.isEmpty() ? field.nameEn : field.nameCn);

    out << QStringLiteral("RFV-ROWS-3")
        << definition.configFile
        << definition.section
        << qint32(definition.fieldCount)
        << qint32(int(definition.layout))
        << qint32(definition.rowLength)
        << qint32(documentRows.size())
        << fieldNames
        << definition.delimiter;

    for (int row : documentRows) {
        if (definition.layout == Layout::Delimited) {
            QStringList values = m_document.values.at(row);
            for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
                if (int(quint32(it.key() >> 32)) != row)
                    continue;
                const int field = int(it.key() & 0xffffffffu);
                if (field >= 0 && field < values.size())
                    values[field] = it.value();
            }
            out << values;
        } else {
            out << effectiveRowBytes(row);
        }
    }
    return data;
}

bool TableModel::appendRowsFromClipboard(const QByteArray& data, QString* error)
{
    if (!m_hasDocument) {
        if (error)
            *error = QStringLiteral("没有打开的文件");
        return false;
    }
    if (!m_filterText.isEmpty()) {
        if (error)
            *error = QStringLiteral("请先清除筛选");
        return false;
    }

    QDataStream in(data);
    in.setVersion(QDataStream::Qt_5_15);

    QString magic;
    QString config;
    QString section;
    qint32 fields = 0;
    qint32 layout = -1;
    qint32 rowLength = 0;
    qint32 count = 0;
    QStringList fieldNames;
    QString delimiter;
    in >> magic >> config >> section >> fields >> layout >> rowLength >> count;
    if (magic == QStringLiteral("RFV-ROWS-3"))
        in >> fieldNames >> delimiter;
    if (in.status() != QDataStream::Ok) {
        if (error)
            *error = QStringLiteral("剪贴板中的行数据不完整");
        return false;
    }

    const InterfaceDefinition& definition = m_document.definition;
    if (magic != QStringLiteral("RFV-ROWS-2")
        && magic != QStringLiteral("RFV-ROWS-3")) {
        if (error)
            *error = QStringLiteral("剪贴板中的行数据格式无法识别");
        return false;
    }

    const bool sameLayout = layout == int(definition.layout)
            && fields == definition.fieldCount;
    bool compatible = sameLayout;
    if (definition.layout == Layout::Delimited) {
        if (magic == QStringLiteral("RFV-ROWS-3")) {
            QStringList currentNames;
            currentNames.reserve(definition.fields.size());
            for (const FieldDefinition& field : definition.fields)
                currentNames.append(field.nameCn.isEmpty() ? field.nameEn : field.nameCn);
            compatible = compatible && fieldNames == currentNames
                    && delimiter == definition.delimiter;
        }
    } else {
        compatible = compatible
                && config == definition.configFile
                && section == definition.section
                && rowLength == definition.rowLength;
    }

    if (!compatible) {
        if (error) {
            *error = definition.layout == Layout::Delimited
                    ? QStringLiteral("复制的行来自不同的内部清算文件（字段数或表头不一致）")
                    : QStringLiteral("复制的行来自其他接口配置，无法粘贴到当前文件");
        }
        return false;
    }
    if (count <= 0 || count > 100000) {
        if (error)
            *error = QStringLiteral("剪贴板中的行数无效");
        return false;
    }

    if (definition.layout == Layout::Delimited) {
        QVector<QStringList> rows;
        rows.reserve(count);
        for (qint32 i = 0; i < count; ++i) {
            QStringList row;
            in >> row;
            if (in.status() != QDataStream::Ok)
                break;
            rows.append(row);
        }
        if (rows.isEmpty()) {
            if (error)
                *error = QStringLiteral("剪贴板中没有有效的行数据");
            return false;
        }
        return appendDelimitedRows(rows);
    }

    QVector<QByteArray> rows;
    rows.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        QByteArray row;
        in >> row;
        if (in.status() != QDataStream::Ok)
            break;
        rows.append(row);
    }
    if (rows.isEmpty()) {
        if (error)
            *error = QStringLiteral("剪贴板中没有有效的行数据");
        return false;
    }
    return appendRows(rows);
}

bool TableModel::appendRows(const QVector<QByteArray>& rows)
{
    if (!m_hasDocument || rows.isEmpty() || !m_filterText.isEmpty())
        return false;
    if (m_document.definition.layout == Layout::Delimited)
        return false;

    const int rowLength = m_document.definition.rowLength;

    beginInsertRows(QModelIndex(), m_visible.size(), m_visible.size() + rows.size() - 1);
    for (QByteArray row : rows) {
        if (m_document.definition.lengthType == 0) {
            if (row.size() < rowLength)
                row.append(QByteArray(rowLength - row.size(), ' '));
            else if (row.size() > rowLength)
                row.truncate(rowLength);
        } else {
            QString line = decodeText(row, m_document.definition.encoding);
            if (line.size() < rowLength)
                line = line.leftJustified(rowLength, QLatin1Char(' '));
            else if (line.size() > rowLength)
                line = line.left(rowLength);
            row = encodeText(line, m_document.definition.encoding);
        }

        const int index = m_document.rows.size();
        m_document.rows.append(row);
        m_document.sourceLines.append(-1);
        m_visible.append(index);
    }
    endInsertRows();
    updateCountLines();
    return true;
}

bool TableModel::appendDelimitedRows(const QVector<QStringList>& rows)
{
    if (!m_hasDocument || rows.isEmpty() || !m_filterText.isEmpty())
        return false;
    if (m_document.definition.layout != Layout::Delimited)
        return false;

    const int fieldCount = m_document.definition.fieldCount;

    beginInsertRows(QModelIndex(), m_visible.size(), m_visible.size() + rows.size() - 1);
    for (QStringList row : rows) {
        while (row.size() < fieldCount)
            row.append(QString());
        if (row.size() > fieldCount)
            row = row.mid(0, fieldCount);

        const int index = m_document.values.size();
        m_document.values.append(row);
        m_document.sourceLines.append(-1);
        m_visible.append(index);
    }
    endInsertRows();
    return true;
}

QString rowsClipboardMimeType()
{
    return QStringLiteral("application/x-regfileviewer-rows");
}

} // namespace rfv