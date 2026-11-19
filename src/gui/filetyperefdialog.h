#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QTabBar;
class QTableWidget;

namespace rfv {

class ConfigRepository;

enum class FileTypeReferenceKind
{
    Ofd,                // 中登 OFD 文件
    WealthManagement    // 理财登定长文件
};

/// 文件类型参考：从 conf 中的接口配置生成“类型代码 -> 文件/交易类型”对照表。
struct FileTypeReferenceRow
{
    QString code;
    QString name;
    QString scope;      // 中登：适用分支，例如“开放式基金”
    QString fileNo;     // 理财登：文件名中的编号，例如“008”
    QStringList versions;
    QStringList sections;
};

class FileTypeReferenceDialog : public QDialog
{
    Q_OBJECT

public:
    FileTypeReferenceDialog(const ConfigRepository& repository,
                            FileTypeReferenceKind kind,
                            QWidget* parent = nullptr);

private:
    void setupOfdTabs(const ConfigRepository& repository);
    void fillWealthTable(const ConfigRepository& repository);
    void populateVersionTabs(int branchIndex);
    void populateOfdTable();
    void applyFilter(const QString& text);

    QTableWidget* m_table = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_countLabel = nullptr;
    QTabBar* m_branchTabs = nullptr;
    QTabBar* m_versionTabs = nullptr;

    QList<FileTypeReferenceRow> m_ofdRows;
    QMap<QString, QStringList> m_branchVersions;
};

} // namespace rfv
