#pragma once

#include "core/fileparser.h"

#include <QChar>
#include <QString>
#include <QVector>

namespace rfv {

class TableModel;

struct ExportOptions
{
    QChar delimiter = QLatin1Char(',');
    bool includeHeader = true;
    bool translate = false;   // CSV 导出时附带字典含义
    bool writeBom = true;
};

class Exporter
{
public:
    /// rows 为 Document 中的行索引；传空表示导出全部行。
    static bool exportCsv(const QString& path, const Document& document,
                          const QVector<int>& rows, const ExportOptions& options,
                          QString* error = nullptr);

    /// 从表格模型导出，包含用户编辑过的单元格与新增列。
    static bool exportCsv(const QString& path, const TableModel& model,
                          const QVector<int>& rows, const ExportOptions& options,
                          QString* error = nullptr);

    static bool exportJson(const QString& path, const Document& document,
                           const QVector<int>& rows, const ExportOptions& options,
                           QString* error = nullptr);

    static bool exportJson(const QString& path, const TableModel& model,
                           const QVector<int>& rows, const ExportOptions& options,
                           QString* error = nullptr);
};

} // namespace rfv
