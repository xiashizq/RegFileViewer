// 控制台自检程序：验证配置加载、文件识别与解析结果。
// 用法：
//   rfv_selftest                 解析 conf/demo 下的全部示例文件
//   rfv_selftest <文件或目录>...  解析指定文件/目录

#include "core/configrepository.h"
#include "core/exporter.h"
#include "core/fileparser.h"
#include "core/tablemodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#endif

namespace {

void printRow(const rfv::Document& document, int row, int maxFields)
{
    const int fields = qMin(document.fieldCount(), maxFields);
    printf("    [line %d] ", document.sourceLines.value(row));
    for (int i = 0; i < fields; ++i) {
        const rfv::FieldDefinition& field = document.definition.fields.at(i);
        const QString value = document.value(row, i);
        printf("%s=%s%s", qUtf8Printable(field.nameEn.isEmpty() ? field.nameCn : field.nameEn),
               qUtf8Printable(value), i + 1 < fields ? " | " : "");
    }
    printf("\n");
}

bool verifyModel(const rfv::Document& document, rfv::TableModel& model)
{
    model.setDocument(document);

    if (document.rowCount() == 0)
        return true;

    if (model.rowCount() != document.rowCount()
        || model.columnCount() != document.fieldCount() + 1) {
        printf("       [FAIL] 表格模型行列数不一致\n");
        return false;
    }

    const QString sample = document.value(0, 0);
    if (!sample.isEmpty()) {
        model.setFilter(sample, false);
        if (model.rowCount() < 1) {
            printf("       [FAIL] 筛选 [%s] 没有命中任何行\n", qUtf8Printable(sample));
            return false;
        }
        model.setFilter(QString(), false);
        if (model.rowCount() != document.rowCount()) {
            printf("       [FAIL] 清除筛选后行数未恢复\n");
            return false;
        }
    }

    const QString firstHeader = model.headerData(0, Qt::Horizontal,
                                                 Qt::DisplayRole).toString();
    if (firstHeader != QStringLiteral("序号")) {
        printf("       [FAIL] 表格模型首列表头不是“序号”\n");
        return false;
    }
    if (model.data(model.index(0, 0), Qt::DisplayRole).toInt() != 1) {
        printf("       [FAIL] 表格模型序号列内容不正确\n");
        return false;
    }

    // 单元格编辑
    const QModelIndex editable = model.index(0, 1);
    const QString original = document.value(0, 0);
    if (!model.setData(editable, QStringLiteral("EDITED"), Qt::EditRole)
        || model.cellValue(0, 0) != QStringLiteral("EDITED")
        || model.modifiedCellCount() < 1) {
        printf("       [FAIL] 单元格编辑失败\n");
        return false;
    }
    model.setData(editable, original, Qt::EditRole);
    if (model.cellValue(0, 0) != original) {
        printf("       [FAIL] 单元格恢复失败\n");
        return false;
    }

    // 保留一个编辑，供保存/复制行测试使用
    if (!model.setData(editable, QStringLiteral("SAVED_VALUE"), Qt::EditRole)
        || !model.hasUnsavedChanges()) {
        printf("       [FAIL] 编辑后的状态不正确\n");
        return false;
    }

    // 复制行：复制第 1 行（含刚才的编辑），插入到其下方
    const int rowsBefore = model.rowCount();
    if (!model.duplicateRows(QVector<int>() << 0)
        || model.rowCount() != rowsBefore + 1) {
        printf("       [FAIL] 复制行失败\n");
        return false;
    }
    if (model.cellValue(1, 0) != QStringLiteral("SAVED_VALUE")) {
        printf("       [FAIL] 复制行未包含编辑内容\n");
        return false;
    }

    // 末尾添加空白行
    if (!model.appendBlankRow() || model.rowCount() != rowsBefore + 2) {
        printf("       [FAIL] 添加空白行失败\n");
        return false;
    }
    const int addedDocRow = model.document()->rowCount() - 1;
    if (!model.cellValue(addedDocRow, 0).isEmpty()) {
        printf("       [FAIL] 空白行不为空\n");
        return false;
    }
    if (!model.hasUnsavedChanges()) {
        printf("       [FAIL] 新增行未标记为未保存\n");
        return false;
    }

    // 整行剪贴板：序列化第 1 行并追加到末尾
    const QByteArray clipboard = model.rowsClipboardData(QVector<int>() << 0);
    if (clipboard.isEmpty()) {
        printf("       [FAIL] 整行剪贴板序列化失败\n");
        return false;
    }
    const int beforePaste = model.rowCount();
    QString clipError;
    if (!model.appendRowsFromClipboard(clipboard, &clipError)
        || model.rowCount() != beforePaste + 1) {
        printf("       [FAIL] 整行粘贴失败：%s\n", qUtf8Printable(clipError));
        return false;
    }
    if (model.cellValue(model.document()->rowCount() - 1, 0)
        != QStringLiteral("SAVED_VALUE")) {
        printf("       [FAIL] 整行粘贴的内容不正确\n");
        return false;
    }

    printf("       [ OK ] 表格模型 / 筛选 / 字典 / 编辑 / 复制行 / 空白行 / 整行粘贴\n");
    return true;
}

bool verifySave(const rfv::Document& document, rfv::TableModel& model,
                const QString& baseName)
{
    QDir dir(QDir::tempPath() + QStringLiteral("/rfv_selftest"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        printf("       [FAIL] 无法创建临时目录\n");
        return false;
    }

    QString suffix = QFileInfo(document.filePath).suffix();
    if (!suffix.isEmpty())
        suffix.prepend(QLatin1Char('.'));
    const QString path = dir.filePath(baseName + QStringLiteral("_saved") + suffix);

    QString error;
    if (!model.saveToFile(path, &error)) {
        printf("       [FAIL] 保存失败：%s\n", qUtf8Printable(error));
        return false;
    }

    if (model.hasUnsavedChanges()) {
        printf("       [FAIL] 保存后仍有未写回的编辑\n");
        return false;
    }

    rfv::Document reloaded;
    if (document.definition.layout == rfv::Layout::Delimited) {
        if (!rfv::FileParser::parseDelimited(path, reloaded, &error)) {
            printf("       [FAIL] 重新解析保存后的文件失败：%s\n", qUtf8Printable(error));
            return false;
        }
    } else if (!rfv::FileParser::parse(path, document.definition, reloaded, &error)) {
        printf("       [FAIL] 重新解析保存后的文件失败：%s\n", qUtf8Printable(error));
        return false;
    }
    if (reloaded.rowCount() != model.rowCount()) {
        printf("       [FAIL] 保存后行数不一致：%d != %d\n",
               reloaded.rowCount(), model.rowCount());
        return false;
    }
    if (reloaded.declaredRecordCount >= 0
        && reloaded.declaredRecordCount != reloaded.rowCount()) {
        printf("       [FAIL] 保存后记录数声明未更新：%d != %d\n",
               reloaded.declaredRecordCount, reloaded.rowCount());
        return false;
    }
    if (reloaded.value(0, 0) != QStringLiteral("SAVED_VALUE")) {
        printf("       [FAIL] 保存后的字段值不正确：%s\n",
               qUtf8Printable(reloaded.value(0, 0)));
        return false;
    }
    if (reloaded.value(1, 0) != QStringLiteral("SAVED_VALUE")) {
        printf("       [FAIL] 保存后复制行的内容不正确\n");
        return false;
    }
    if (reloaded.value(reloaded.rowCount() - 1, 0) != QStringLiteral("SAVED_VALUE")) {
        printf("       [FAIL] 保存后粘贴行的内容不正确\n");
        return false;
    }

    printf("       [ OK ] 保存回写 / 重新解析校验 -> %s\n", qUtf8Printable(path));
    return true;
}

bool verifyExport(const rfv::Document& document, const rfv::TableModel& model,
                  const QString& baseName)
{
    QDir dir(QDir::tempPath() + QStringLiteral("/rfv_selftest"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        printf("       [FAIL] 无法创建临时导出目录\n");
        return false;
    }

    const QString csvPath = dir.filePath(baseName + QStringLiteral(".csv"));
    const QString jsonPath = dir.filePath(baseName + QStringLiteral(".json"));
    QString error;

    if (!rfv::Exporter::exportCsv(csvPath, model, QVector<int>(),
                                  rfv::ExportOptions(), &error)) {
        printf("       [FAIL] CSV 导出：%s\n", qUtf8Printable(error));
        return false;
    }
    if (!rfv::Exporter::exportJson(jsonPath, model, QVector<int>(),
                                   rfv::ExportOptions(), &error)) {
        printf("       [FAIL] JSON 导出：%s\n", qUtf8Printable(error));
        return false;
    }

    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        printf("       [FAIL] 无法读取导出的 JSON\n");
        return false;
    }
    const QJsonDocument json = QJsonDocument::fromJson(jsonFile.readAll());
    jsonFile.close();

    if (!json.isObject()) {
        printf("       [FAIL] 导出的 JSON 不是对象\n");
        return false;
    }
    const QJsonArray rows = json.object().value(QStringLiteral("rows")).toArray();
    const QJsonArray fields = json.object().value(QStringLiteral("fields")).toArray();
    if (fields.size() != model.dataColumnCount()) {
        printf("       [FAIL] JSON 字段数 %d != %d\n", int(fields.size()),
               model.dataColumnCount());
        return false;
    }
    if (rows.size() != model.rowCount()) {
        printf("       [FAIL] JSON 行数 %d != %d\n", int(rows.size()), model.rowCount());
        return false;
    }
    if (!rows.isEmpty()) {
        const QJsonObject first = rows.at(0).toObject();
        if (!first.contains(QStringLiteral("_line"))) {
            printf("       [FAIL] JSON 行缺少 _line 字段\n");
            return false;
        }
    }

    printf("       [ OK ] 导出 CSV %lld 字节 / JSON %lld 字节 -> %s\n",
           static_cast<long long>(QFileInfo(csvPath).size()),
           static_cast<long long>(QFileInfo(jsonPath).size()),
           qUtf8Printable(dir.absolutePath()));
    return true;
}

bool verifyDelete(const rfv::Document& document, const QString& baseName)
{
    if (document.rowCount() == 0)
        return true;

    QDir dir(QDir::tempPath() + QStringLiteral("/rfv_selftest"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        printf("       [FAIL] 无法创建临时删除测试目录\n");
        return false;
    }

    QString suffix = QFileInfo(document.filePath).suffix();
    if (!suffix.isEmpty())
        suffix.prepend(QLatin1Char('.'));
    const QString path = dir.filePath(baseName + QStringLiteral("_deleted") + suffix);

    rfv::TableModel model;
    model.setDocument(document);
    const int before = model.rowCount();
    if (!model.deleteRows(QVector<int>() << 0)
        || model.rowCount() != before - 1
        || !model.hasUnsavedChanges()) {
        printf("       [FAIL] 删除行失败\n");
        return false;
    }

    QString error;
    if (!model.saveToFile(path, &error)) {
        printf("       [FAIL] 删除行后保存失败：%s\n", qUtf8Printable(error));
        return false;
    }
    if (model.hasUnsavedChanges()) {
        printf("       [FAIL] 删除行保存后仍标记为未保存\n");
        return false;
    }

    rfv::Document reloaded;
    if (document.definition.layout == rfv::Layout::Delimited) {
        if (!rfv::FileParser::parseDelimited(path, reloaded, &error)) {
            printf("       [FAIL] 删除行后重新解析失败：%s\n", qUtf8Printable(error));
            return false;
        }
    } else if (!rfv::FileParser::parse(path, document.definition, reloaded, &error)) {
        printf("       [FAIL] 删除行后重新解析失败：%s\n", qUtf8Printable(error));
        return false;
    }

    const int expected = before - 1;
    if (reloaded.rowCount() != expected) {
        printf("       [FAIL] 删除行后行数不一致：%d != %d\n",
               reloaded.rowCount(), expected);
        return false;
    }

    // 删除行后，内存文件头中的记录数统计行必须立即同步
    if (!document.definition.isDelimited()) {
        const int recordRow = document.definition.isOfd()
                ? 6 : document.definition.rowCountCheckRow;
        if (recordRow > 0 && recordRow <= model.document()->headerLines.size()) {
            bool ok = false;
            const int value = model.document()->headerLines.at(recordRow - 1)
                    .trimmed().toInt(&ok);
            if (!ok || value != model.rowCount()) {
                printf("       [FAIL] 删除行后内存统计行未同步：%d != %d\n",
                       value, model.rowCount());
                return false;
            }
        }
    }

    if (!document.definition.isDelimited()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            printf("       [FAIL] 无法读取删除后的文件\n");
            return false;
        }
        const QList<QByteArray> rawLines = file.readAll().split('\n');
        file.close();

        const auto lineValue = [&rawLines](int rowNumber) {
            if (rowNumber <= 0 || rowNumber > rawLines.size())
                return QString();
            QByteArray line = rawLines.at(rowNumber - 1);
            if (line.endsWith('\r'))
                line.chop(1);
            return QString::fromLatin1(line).trimmed();
        };

        const int countRow = document.definition.isOfd()
                ? 6 : document.definition.rowCountCheckRow;
        bool countOk = true;
        if (countRow > 0) {
            bool ok = false;
            const int value = lineValue(countRow).toInt(&ok);
            countOk = ok && value == expected;
        }
        const int extraCountRow = document.definition.dataRecordCountRow;
        if (countOk && document.definition.isOfd() && extraCountRow > 0) {
            bool ok = false;
            const int value = lineValue(extraCountRow).toInt(&ok);
            countOk = ok && value == expected;
        }
        if (!countOk) {
            printf("       [FAIL] 删除行后记录数未同步更新\n");
            return false;
        }
    }

    printf("       [ OK ] 删除行 / 行数与统计值同步 -> %s\n", qUtf8Printable(path));
    return true;
}

/// 修改发送方/接收方/文件日期后保存：OFD 写回报文头，全部格式同步重命名文件。
bool verifyMetadata(const rfv::Document& document, const QString& baseName)
{
    if (document.rowCount() == 0)
        return true;

    QDir dir(QDir::tempPath() + QStringLiteral("/rfv_selftest"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        printf("       [FAIL] 无法创建临时元数据测试目录\n");
        return false;
    }

    QString suffix = QFileInfo(document.filePath).suffix();
    if (!suffix.isEmpty())
        suffix.prepend(QLatin1Char('.'));
    const QString path = dir.filePath(baseName + QStringLiteral("_meta") + suffix);

    rfv::TableModel model;
    model.setDocument(document);

    const bool delimited = document.isDelimited();
    model.setHeaderMetadata(QStringLiteral("NEWSEND"),
                            delimited ? QString() : QStringLiteral("NEWRCVR"),
                            QStringLiteral("20261231"));
    if (!model.hasUnsavedChanges()) {
        printf("       [FAIL] 修改元数据后未标记为未保存\n");
        return false;
    }

    QString error;
    if (!model.saveToFile(path, &error)) {
        printf("       [FAIL] 元数据保存失败：%s\n", qUtf8Printable(error));
        return false;
    }
    if (model.hasUnsavedChanges()) {
        printf("       [FAIL] 元数据保存后仍标记为未保存\n");
        return false;
    }

    // 文件名符合四段结构时，保存后应按新元数据重命名，且其余段保持不变
    const QChar separator = delimited ? QLatin1Char('_') : QLatin1Char('-');
    const bool renameable = baseName.split(separator).size() >= 4;
    const QString finalPath = model.document()->filePath;
    if (renameable) {
        const QStringList parts = QFileInfo(finalPath).completeBaseName().split(separator);
        if (parts.size() < 4 || parts.at(0) != QStringLiteral("NEWSEND")
            || parts.at(3) != QStringLiteral("20261231")
            || (!delimited && parts.at(1) != QStringLiteral("NEWRCVR"))) {
            printf("       [FAIL] 保存后文件名未同步元数据：%s\n",
                   qUtf8Printable(QFileInfo(finalPath).fileName()));
            return false;
        }
    }

    rfv::Document reloaded;
    if (delimited) {
        if (!rfv::FileParser::parseDelimited(finalPath, reloaded, &error)) {
            printf("       [FAIL] 重命名后重新解析失败：%s\n", qUtf8Printable(error));
            return false;
        }
    } else if (!rfv::FileParser::parse(finalPath, document.definition,
                                       reloaded, &error)) {
        printf("       [FAIL] 重命名后重新解析失败：%s\n", qUtf8Printable(error));
        return false;
    }

    if (reloaded.senderCode != QStringLiteral("NEWSEND")
        || reloaded.fileDate != QStringLiteral("20261231")
        || (!delimited && reloaded.receiverCode != QStringLiteral("NEWRCVR"))) {
        printf("       [FAIL] 重新解析后的发送方/接收方/日期不正确：%s / %s / %s\n",
               qUtf8Printable(reloaded.senderCode), qUtf8Printable(reloaded.receiverCode),
               qUtf8Printable(reloaded.fileDate));
        return false;
    }

    // OFD：报文头第 3/4/5 行应写入新值
    if (document.definition.isOfd()
        && (reloaded.headerLines.size() < 5
            || reloaded.headerLines.at(2).trimmed() != QStringLiteral("NEWSEND")
            || reloaded.headerLines.at(3).trimmed() != QStringLiteral("NEWRCVR")
            || reloaded.headerLines.at(4).trimmed() != QStringLiteral("20261231"))) {
        printf("       [FAIL] 报文头的发送方/接收方/日期未写回\n");
        return false;
    }

    printf("       [ OK ] 元数据编辑 / 报文头写回与文件重命名 -> %s\n",
           qUtf8Printable(finalPath));
    return true;
}

bool runFile(rfv::ConfigRepository& repository, const QString& path)
{
    rfv::FileProbe probe;
    QString error;
    if (!rfv::FileParser::probe(path, probe, &error)) {
        printf("[FAIL] %s: %s\n", qUtf8Printable(path), qUtf8Printable(error));
        return false;
    }

    rfv::Document document;
    rfv::InterfaceDefinition definition;
    int candidateCount = 0;

    if (probe.isDelimited) {
        if (!rfv::FileParser::parseDelimited(path, document, &error)) {
            printf("[FAIL] %s: %s\n", qUtf8Printable(path), qUtf8Printable(error));
            return false;
        }
        definition = document.definition;
    } else {
        QList<rfv::InterfaceDefinition> candidates =
                repository.candidates(probe.isOfd, probe.version, probe.fileType,
                                      probe.fileName, probe.firstLine);
        if (candidates.isEmpty()) {
            printf("[FAIL] %s: 没有匹配的解析方案 (首行=%s, 版本=%s, 类型=%s)\n",
                   qUtf8Printable(QFileInfo(path).fileName()), qUtf8Printable(probe.firstLine),
                   qUtf8Printable(probe.version), qUtf8Printable(probe.fileType));
            return false;
        }

        definition = candidates.first();
        if (!rfv::FileParser::parse(path, definition, document, &error)) {
            printf("[FAIL] %s: %s\n", qUtf8Printable(path), qUtf8Printable(error));
            return false;
        }
        candidateCount = candidates.size();
    }

    printf("[ OK ] %s\n", qUtf8Printable(QFileInfo(path).fileName()));
    printf("       协议: %s %s\n", qUtf8Printable(definition.protocol),
           qUtf8Printable(definition.versionLabel));
    printf("       方案: %s  [%s]  候选数=%d\n", qUtf8Printable(definition.describe),
           qUtf8Printable(definition.section), candidateCount);
    printf("       字段: %d  行长度: %d  数据起始行: %d\n",
           definition.fieldCount, definition.rowLength,
           document.definition.dataBeginRow);
    printf("       记录: 声明=%d 实际=%d  结束标识=%s  耗时=%dms\n",
           document.declaredRecordCount, document.rowCount(),
           document.sawEndMarker ? "有" : "无", document.parseMilliseconds);

    for (const QString& warning : document.warnings)
        printf("       [WARN] %s\n", qUtf8Printable(warning));

    const int sampleRows = qMin(document.rowCount(), 3);
    for (int row = 0; row < sampleRows; ++row)
        printRow(document, row, 8);

    const QString baseName = QFileInfo(path).completeBaseName();
    rfv::TableModel model;
    const bool modelOk = verifyModel(document, model);
    const bool saveOk = verifySave(document, model, baseName);
    const bool exportOk = verifyExport(document, model, baseName);
    const bool deleteOk = verifyDelete(document, baseName);
    const bool metaOk = verifyMetadata(document, baseName);

    printf("\n");
    return document.rowCount() > 0 && document.warnings.isEmpty()
            && modelOk && saveOk && exportOk && deleteOk && metaOk;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

#ifdef Q_OS_WIN
    ::SetConsoleOutputCP(CP_UTF8);
    ::_setmode(::_fileno(stdout), _O_BINARY);
#endif

    // 清理上次运行残留的临时文件：verifyMetadata 会把保存的文件重命名为
    // 固定的新元数据文件名，目标已存在时按设计不覆盖，残留会让
    // 连续运行两次自检时误报“保存后文件名未同步元数据”。
    {
        QDir staleDir(QDir::tempPath() + QStringLiteral("/rfv_selftest"));
        if (staleDir.exists()) {
            const QFileInfoList staleFiles =
                    staleDir.entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo& entry : staleFiles)
                QFile::remove(entry.absoluteFilePath());
        }
    }

    rfv::ConfigRepository repository;
    const QString confDir = rfv::ConfigRepository::locateConfDir();

    // 触发 QVector::operator==，用于覆盖 Qt 5.15 + 新版 MSVC 的 stdext 兼容问题
    {
        const QVector<int> left = QVector<int>() << 1 << 2 << 3;
        const QVector<int> right = QVector<int>() << 1 << 2 << 3;
        if (!(left == right)) {
            printf("[FAIL] QVector::operator== 结果异常\n");
            return 2;
        }
    }

    QStringList messages;
    if (confDir.isEmpty() || !repository.load(confDir, &messages)) {
        printf("[FAIL] 无法加载 conf：%s\n", qUtf8Printable(confDir));
        for (const QString& message : messages)
            printf("       %s\n", qUtf8Printable(message));
        return 2;
    }

    // 回归检查：N 字段值自带小数点时不产生双小数点
    {
        rfv::FieldDefinition numericField;
        numericField.type = QLatin1Char('N');
        numericField.length = 16;
        numericField.decimals = 2;

        struct Sample { const char* raw; const char* expected; };
        const Sample samples[] = {
            { "123.45",  "123.45" },
            { "0001.50", "1.50"   },
            { ".5",      "0.5"    },
            { "-001.5",  "-1.5"   },
        };
        bool ok = true;
        for (const Sample& sample : samples) {
            const QString actual = rfv::formatFieldValue(QString::fromLatin1(sample.raw),
                                                         numericField);
            if (actual != QString::fromLatin1(sample.expected)) {
                printf("[FAIL] formatFieldValue(%s) = %s，期望 %s\n", sample.raw,
                       qUtf8Printable(actual), sample.expected);
                ok = false;
            }
        }
        if (ok)
            printf("[ OK ] formatFieldValue：带小数点的 N 字段值规范化正确\n");
        else
            return 2;
    }

    // 回归检查：分隔符文件中列数多于表头的行要给出诊断提示（不再静默截断）
    {
        const QDir scratch(QDir::tempPath() + QStringLiteral("/rfv_selftest"));
        if (!scratch.exists() && !scratch.mkpath(QStringLiteral(".")))
            return 2;
        const QString path = scratch.filePath(QStringLiteral("_extra_columns.txt"));
        {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                printf("[FAIL] 无法创建多余列测试文件：%s\n", qUtf8Printable(path));
                return 2;
            }
            file.write(QStringLiteral(
                    "元数据行\n"
                    "序号|名称|金额\n"
                    "1|张三|100\n"
                    "2|李四|200|备注\n")
                    .toUtf8());
        }

        rfv::Document document;
        QString error;
        if (!rfv::FileParser::parseDelimited(path, document, &error)) {
            printf("[FAIL] 多余列测试解析失败：%s\n", qUtf8Printable(error));
            return 2;
        }
        QFile::remove(path);

        bool warned = false;
        for (const QString& warning : document.warnings) {
            if (warning.contains(QStringLiteral("多于表头")))
                warned = true;
        }
        if (document.rowCount() != 2 || !warned) {
            printf("[FAIL] 列数多于表头时诊断提示缺失（行数=%d，提示数=%d）\n",
                   document.rowCount(), document.warnings.size());
            return 2;
        }
        printf("[ OK ] parseDelimited：列数多于表头时输出诊断提示\n");
    }

    printf("conf     : %s\n", qUtf8Printable(confDir));
    printf("OFD 方案 : %d\n", int(repository.ofdDefinitions().size()));
    printf("定长方案 : %d\n\n", int(repository.fixedDefinitions().size()));

    QStringList paths;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        const QFileInfo info(arguments.at(i));
        if (info.isDir()) {
            const QFileInfoList entries = QDir(info.absoluteFilePath())
                    .entryInfoList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.TXT"),
                                   QDir::Files, QDir::Name);
            for (const QFileInfo& entry : entries)
                paths.append(entry.absoluteFilePath());
        } else {
            paths.append(info.absoluteFilePath());
        }
    }

    if (paths.isEmpty()) {
        const QString demoDir = confDir + QStringLiteral("/demo");
        const QFileInfoList entries = QDir(demoDir)
                .entryInfoList(QStringList() << QStringLiteral("*.txt") << QStringLiteral("*.TXT"),
                               QDir::Files, QDir::Name);
        for (const QFileInfo& entry : entries)
            paths.append(entry.absoluteFilePath());
        if (paths.isEmpty()) {
            printf("没有找到待解析的文件（conf/demo 为空）\n");
            return 3;
        }
    }

    int failed = 0;
    for (const QString& path : paths) {
        if (!runFile(repository, path))
            ++failed;
    }

    printf("==== 共 %d 个文件，完全通过 %d，存在提示/失败 %d ====\n",
           int(paths.size()), int(paths.size()) - failed, failed);
    return failed == 0 ? 0 : 1;
}
