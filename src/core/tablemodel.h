#pragma once

#include "core/fileparser.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QRegularExpression>
#include <QVector>

namespace rfv {

/// 表格模型：每列对应接口字段或用户新增的列。
/// 字段值按需从原始字节切片解码；编辑内容保存在内存中，导出时生效。
class TableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit TableModel(QObject* parent = nullptr);

    void setDocument(Document document);
    void clear();
    const Document* document() const { return m_hasDocument ? &m_document : nullptr; }

    void setFilter(const QString& text, bool regularExpression);
    QString filterText() const { return m_filterText; }
    bool filterIsRegularExpression() const { return m_filterRegex; }

    void setTranslateDictionary(bool enabled);
    bool translateDictionary() const { return m_translate; }

    void setShowEnglishHeader(bool enabled);
    bool showEnglishHeader() const { return m_showEnglish; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// 当前可见行对应的 Document 行索引
    const QVector<int>& visibleRows() const { return m_visible; }

    QString translatedValue(int documentRow, int fieldIndex) const;

    // ---- 列与单元格 -------------------------------------------------------

    /// 数据列数（接口字段 + 用户新增列）
    int dataColumnCount() const;
    QString dataColumnHeader(int dataColumn) const;
    bool isExtraColumn(int dataColumn) const;
    int extraColumnCount() const { return m_extraNames.size(); }

    /// 获取单元格最终值（包含用户编辑），documentRow 为 Document 行索引
    QString cellValue(int documentRow, int dataColumn) const;

    /// 在末尾追加一个空白列，返回新列名
    bool appendEmptyColumn(QString* columnName = nullptr);

    /// 复制表格列（dataColumn 为数据列索引，从 0 开始）到末尾
    bool duplicateColumn(int dataColumn, QString* columnName = nullptr);

    /// 重命名新增列（dataColumn 为数据列索引，从 0 开始）；接口字段列不允许重命名
    bool renameColumn(int dataColumn, const QString& name);

    int modifiedCellCount() const;

    /// 是否存在尚未写回的修改（字段编辑或新增行）
    bool hasUnsavedChanges() const;

    /// 尚未写入文件的新增行数量
    int addedRowCount() const;

    /// 新增列中是否存在内容（新增列不会写入接口文件）
    bool hasExtraColumnValues() const;

    /// 把编辑和新增行按定长规则写回文件；保留报文头、行尾、文件尾与编码
    bool saveToFile(const QString& path, QString* error = nullptr,
                    int* droppedBlankRows = nullptr);

    /// 修改发送方/接收方/文件日期：OFD 写入报文头第 3/4/5 行，
    /// 内部清算与定长文件通过保存时的文件重命名生效
    void setHeaderMetadata(const QString& sender, const QString& receiver,
                           const QString& date);

    /// 在末尾添加一个空白行
    bool appendBlankRow();

    /// 复制指定的可见行，插入到最后一个选中行之后
    bool duplicateRows(const QVector<int>& visibleRows);

    /// 删除指定的可见行（含新增行）
    bool deleteRows(const QVector<int>& visibleRows);

    /// 整行剪贴板：序列化指定的文档行（含编辑内容）
    QByteArray rowsClipboardData(const QVector<int>& documentRows) const;

    /// 从整行剪贴板数据追加到数据末尾（校验接口配置是否一致）
    bool appendRowsFromClipboard(const QByteArray& data, QString* error = nullptr);

    /// 追加原始行内容到末尾（不足补空格，超长截断）
    bool appendRows(const QVector<QByteArray>& rows);

private:
    void rebuildVisibleRows();
    void updateCountLines();
    QString metadataTargetPath(const QString& path) const;
    QString renameForMetadata(const QString& writtenPath) const;
    QString fieldHeaderText(const FieldDefinition& field) const;
    bool applyFieldEdit(QByteArray& row, int fieldIndex, const QString& value,
                        QString* error) const;
    void shiftRowKeys(int insertAt, int count);
    QByteArray effectiveRowBytes(int documentRow) const;
    bool saveDelimitedFile(const QString& path, QString* error, int* droppedBlankRows);
    bool appendDelimitedRows(const QVector<QStringList>& rows);
    static quint64 editKey(int documentRow, int dataColumn);

    Document m_document;
    bool m_hasDocument = false;
    QVector<int> m_visible;
    QString m_filterText;
    bool m_filterRegex = false;
    bool m_translate = true;
    bool m_showEnglish = false;

    QStringList m_extraNames;
    QVector<QHash<int, QString>> m_extraValues; // 每个新增列：Document 行 -> 值
    QHash<quint64, QString> m_edits;            // 接口字段的编辑结果
    bool m_hasRemovedRows = false;              // 是否删除过行，删除也需要写回
    bool m_metaChanged = false;                 // 发送方/接收方/文件日期是否被修改
};

/// 整行复制使用的自定义剪贴板 MIME 类型
QString rowsClipboardMimeType();

} // namespace rfv
