#include "core/exporter.h"

#include "core/tablemodel.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <functional>

namespace {

struct TableSource
{
    const rfv::Document* document = nullptr;
    int columns = 0;
    QStringList headers;
    QStringList jsonKeys;
    QJsonArray fieldMeta;
    std::function<QString(int, int)> value;    // 文档行, 数据列
    std::function<QString(int, int)> meaning;  // 字典含义，可为空
};

QString csvEscape(const QString& value, QChar delimiter)
{
    const bool needQuote = value.contains(delimiter)
            || value.contains(QLatin1Char('"'))
            || value.contains(QLatin1Char('\n'))
            || value.contains(QLatin1Char('\r'));
    if (!needQuote)
        return value;

    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString dictionaryMeaning(const rfv::Document& document, int fieldIndex, const QString& value)
{
    if (!document.definition.dictionaries)
        return QString();
    if (fieldIndex < 0 || fieldIndex >= document.definition.fields.size())
        return QString();

    const rfv::FieldDefinition& field = document.definition.fields.at(fieldIndex);
    const auto& dictionaries = *document.definition.dictionaries;

    auto it = dictionaries.constFind(field.nameEn);
    if (it == dictionaries.constEnd())
        it = dictionaries.constFind(field.nameCn);
    if (it == dictionaries.constEnd())
        return QString();

    const auto valueIt = it->constFind(value);
    return valueIt == it->constEnd() ? QString() : *valueIt;
}

QString uniqueKey(QString key, QSet<QString>& used)
{
    if (key.isEmpty())
        key = QStringLiteral("Field");
    QString candidate = key;
    int suffix = 2;
    while (used.contains(candidate))
        candidate = QStringLiteral("%1_%2").arg(key).arg(suffix++);
    used.insert(candidate);
    return candidate;
}

QJsonObject fieldObject(int index, const QString& name, const QString& nameEn,
                        const QString& type, int length, int decimals)
{
    QJsonObject object;
    object.insert(QStringLiteral("index"), index);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("nameEn"), nameEn);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("length"), length);
    object.insert(QStringLiteral("decimals"), decimals);
    return object;
}

TableSource makeDocumentSource(const rfv::Document& document)
{
    TableSource source;
    source.document = &document;
    source.columns = document.definition.fields.size();

    QSet<QString> used;
    for (int i = 0; i < source.columns; ++i) {
        const rfv::FieldDefinition& field = document.definition.fields.at(i);
        source.headers.append(field.nameCn.isEmpty() ? field.nameEn : field.nameCn);
        source.jsonKeys.append(uniqueKey(field.nameEn.isEmpty() ? field.nameCn : field.nameEn,
                                         used));
        source.fieldMeta.append(fieldObject(field.index, field.nameCn, field.nameEn,
                                            QString(field.type), field.length, field.decimals));
    }

    source.value = [&document](int row, int column) {
        return document.value(row, column);
    };
    source.meaning = [&document](int row, int column) {
        return dictionaryMeaning(document, column, document.value(row, column));
    };
    return source;
}

TableSource makeModelSource(const rfv::TableModel& model)
{
    TableSource source;
    source.document = model.document();
    source.columns = model.dataColumnCount();

    QSet<QString> used;
    const int fieldCount = source.document
            ? source.document->definition.fields.size() : 0;

    for (int column = 0; column < source.columns; ++column) {
        const QString header = model.dataColumnHeader(column);
        source.headers.append(header);

        if (column < fieldCount) {
            const rfv::FieldDefinition& field = source.document->definition.fields.at(column);
            source.jsonKeys.append(uniqueKey(field.nameEn.isEmpty() ? field.nameCn
                                                                    : field.nameEn, used));
            source.fieldMeta.append(fieldObject(field.index, field.nameCn, field.nameEn,
                                                QString(field.type), field.length,
                                                field.decimals));
        } else {
            source.jsonKeys.append(uniqueKey(header, used));
            source.fieldMeta.append(fieldObject(column + 1, header, header,
                                                QStringLiteral("TEXT"), 0, 0));
        }
    }

    source.value = [&model](int row, int column) {
        return model.cellValue(row, column);
    };
    source.meaning = [&model](int row, int column) {
        return model.translatedValue(row, column);
    };
    return source;
}

QVector<int> effectiveRows(const rfv::Document& document, const QVector<int>& rows)
{
    QVector<int> result;
    if (rows.isEmpty()) {
        result.reserve(document.rowCount());
        for (int i = 0; i < document.rowCount(); ++i)
            result.append(i);
        return result;
    }

    result.reserve(rows.size());
    for (int row : rows) {
        if (row >= 0 && row < document.rowCount())
            result.append(row);
    }
    return result;
}

bool writeCsv(const QString& path, const TableSource& source, const QVector<int>& rows,
              const rfv::ExportOptions& options, QString* error)
{
    if (!source.document) {
        if (error)
            *error = QStringLiteral("没有可导出的数据");
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif
    stream.setGenerateByteOrderMark(options.writeBom);

    if (options.includeHeader) {
        QStringList header;
        header.append(QStringLiteral("源行号"));
        for (const QString& name : source.headers)
            header.append(csvEscape(name, options.delimiter));
        stream << header.join(options.delimiter) << "\n";
    }

    const QVector<int> exportRows = effectiveRows(*source.document, rows);
    for (int documentRow : exportRows) {
        QStringList values;
        values.reserve(source.columns + 1);
        values.append(QString::number(source.document->sourceLines.value(documentRow)));

        for (int column = 0; column < source.columns; ++column) {
            QString cell = source.value(documentRow, column);
            if (options.translate) {
                const QString meaning = source.meaning(documentRow, column);
                if (!meaning.isEmpty() && meaning != cell)
                    cell += QLatin1Char('/') + meaning;
            }
            values.append(csvEscape(cell, options.delimiter));
        }

        stream << values.join(options.delimiter) << "\n";
    }

    stream.flush();
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("保存文件失败：%1").arg(path);
        return false;
    }
    return true;
}

bool writeJson(const QString& path, const TableSource& source, const QVector<int>& rows,
               QString* error)
{
    if (!source.document) {
        if (error)
            *error = QStringLiteral("没有可导出的数据");
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }

    const QVector<int> exportRows = effectiveRows(*source.document, rows);

    QJsonObject meta;
    meta.insert(QStringLiteral("file"),
                QFileInfo(source.document->filePath).fileName());
    meta.insert(QStringLiteral("protocol"), source.document->definition.protocol);
    meta.insert(QStringLiteral("version"), source.document->definition.versionLabel);
    meta.insert(QStringLiteral("interface"), source.document->definition.describe);
    meta.insert(QStringLiteral("section"), source.document->definition.section);
    meta.insert(QStringLiteral("encoding"), source.document->definition.encoding);
    meta.insert(QStringLiteral("layout"), rfv::layoutLabel(source.document->definition.layout));
    meta.insert(QStringLiteral("recordCount"), exportRows.size());
    meta.insert(QStringLiteral("fields"), source.fieldMeta);

    QByteArray metaBytes = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    metaBytes.chop(1); // 去掉结尾的 '}'
    file.write(metaBytes);
    file.write(",\"rows\":[\n");

    for (int i = 0; i < exportRows.size(); ++i) {
        const int documentRow = exportRows.at(i);
        QJsonObject object;
        object.insert(QStringLiteral("_line"),
                      source.document->sourceLines.value(documentRow));
        for (int column = 0; column < source.columns; ++column)
            object.insert(source.jsonKeys.at(column), source.value(documentRow, column));

        QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (i + 1 < exportRows.size())
            line += ',';
        line += '\n';
        file.write(line);
    }

    file.write("]}\n");

    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("保存文件失败：%1").arg(path);
        return false;
    }
    return true;
}

} // namespace

bool rfv::Exporter::exportCsv(const QString& path, const Document& document,
                              const QVector<int>& rows, const ExportOptions& options,
                              QString* error)
{
    return writeCsv(path, makeDocumentSource(document), rows, options, error);
}

bool rfv::Exporter::exportCsv(const QString& path, const TableModel& model,
                              const QVector<int>& rows, const ExportOptions& options,
                              QString* error)
{
    return writeCsv(path, makeModelSource(model), rows, options, error);
}

bool rfv::Exporter::exportJson(const QString& path, const Document& document,
                               const QVector<int>& rows, const ExportOptions& options,
                               QString* error)
{
    Q_UNUSED(options)
    return writeJson(path, makeDocumentSource(document), rows, error);
}

bool rfv::Exporter::exportJson(const QString& path, const TableModel& model,
                               const QVector<int>& rows, const ExportOptions& options,
                               QString* error)
{
    Q_UNUSED(options)
    return writeJson(path, makeModelSource(model), rows, error);
}
