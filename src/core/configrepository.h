#pragma once

#include "core/interface.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace rfv {

struct FileProbe;

/// 加载 conf 目录下的全部接口解析配置。
class ConfigRepository
{
public:
    bool load(const QString& confDir, QStringList* messages = nullptr);

    /// 依次尝试：环境变量 RFV_CONF_DIR、程序目录下 conf、源码目录 conf
    static QString locateConfDir();

    const QVector<InterfaceDefinition>& ofdDefinitions() const { return m_ofd; }
    const QVector<InterfaceDefinition>& fixedDefinitions() const { return m_fixed; }

    /// 根据探测结果挑选候选解析方案（按匹配程度排序）
    QList<InterfaceDefinition> candidates(bool isOfd, const QString& version,
                                          const QString& fileType,
                                          const QString& fileName,
                                          const QString& firstLine) const;

    /// 发送方/接收方代码 -> 机构名称
    QString codeName(const QString& code) const;

    QString confDir() const { return m_confDir; }
    const QStringList& warnings() const { return m_warnings; }

private:
    void loadOfdFile(const QString& path);
    void loadFixedFile(const QString& path);
    void loadDictionaries(const QString& path);
    void loadCodes(const QString& path);
    void applyFieldTips(InterfaceDefinition& def, const class IniFile& ini) const;

    QString m_confDir;
    QVector<InterfaceDefinition> m_ofd;
    QVector<InterfaceDefinition> m_fixed;
    QHash<QString, FieldDictionaryPtr> m_ofdDictionaries;
    QHash<QString, QString> m_codes;
    QStringList m_warnings;
};

QString protocolNameForConfig(const QString& fileName);

/// 文件头声明的字段名与配置字段名的匹配个数，用于在多套候选配置中自动择优。
int headerFieldMatchCount(const InterfaceDefinition& definition,
                          const QStringList& headerFieldNames);

} // namespace rfv
