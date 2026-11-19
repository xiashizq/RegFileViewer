#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace rfv {

/// 轻量 INI 解析器。
/// 之所以不用 QSettings：接口配置的段名里带有 '?'、'*'、'|' 等字符，
/// 且需要严格保留键名大小写和原始顺序。
class IniFile
{
public:
    struct Section
    {
        QString name;
        QHash<QString, QString> values;
        QStringList keyOrder;
    };

    bool load(const QString& path, QString* error = nullptr);

    QStringList sectionNames() const;
    bool contains(const QString& section) const;
    QString value(const QString& section, const QString& key,
                  const QString& defaultValue = QString()) const;
    const Section* section(const QString& name) const;
    const QVector<Section>& sections() const { return m_sections; }

private:
    QVector<Section> m_sections;
    QHash<QString, int> m_index;
};

} // namespace rfv
