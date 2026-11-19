#include "core/inifile.h"

#include <QFile>
#include <QTextStream>

namespace {

QString stripInlineComment(const QString& value)
{
    // 仅处理 “值 + 空白 + ;注释” 这种常见写法，避免误伤值里的分号
    bool inQuotes = false;
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('"'))
            inQuotes = !inQuotes;
        else if (!inQuotes && (c == QLatin1Char(';') || c == QLatin1Char('#'))) {
            if (i == 0 || value.at(i - 1).isSpace())
                return value.left(i).trimmed();
        }
    }
    return value;
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
        && value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

} // namespace

bool rfv::IniFile::load(const QString& path, QString* error)
{
    m_sections.clear();
    m_index.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开配置文件：%1").arg(path);
        return false;
    }

    QByteArray raw = file.readAll();
    file.close();

    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);

    const QString text = QString::fromUtf8(raw);
    const QStringList lines = text.split(QLatin1Char('\n'));

    Section* current = nullptr;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith(QLatin1Char(';')) || trimmed.startsWith(QLatin1Char('#')))
            continue;

        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            const QString name = trimmed.mid(1, trimmed.size() - 2).trimmed();
            if (!m_index.contains(name)) {
                Section section;
                section.name = name;
                m_sections.append(section);
                m_index.insert(name, m_sections.size() - 1);
            }
            current = &m_sections[m_index.value(name)];
            continue;
        }

        if (!current)
            continue;

        const int eq = trimmed.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;

        const QString key = trimmed.left(eq).trimmed();
        QString value = stripInlineComment(trimmed.mid(eq + 1));
        value = unquote(value);

        if (!current->values.contains(key))
            current->keyOrder.append(key);
        current->values.insert(key, value);
    }

    return true;
}

QStringList rfv::IniFile::sectionNames() const
{
    QStringList names;
    names.reserve(m_sections.size());
    for (const Section& section : m_sections)
        names.append(section.name);
    return names;
}

bool rfv::IniFile::contains(const QString& section) const
{
    return m_index.contains(section);
}

QString rfv::IniFile::value(const QString& section, const QString& key,
                            const QString& defaultValue) const
{
    const auto it = m_index.constFind(section);
    if (it == m_index.constEnd())
        return defaultValue;

    const Section& target = m_sections.at(*it);
    const auto exact = target.values.constFind(key);
    if (exact != target.values.constEnd())
        return *exact;

    // 配置里大小写并不完全统一（OFD 段用 COUNT，定长段用 fieldcount），
    // 因此再按忽略大小写查找一次。
    for (auto valueIt = target.values.constBegin(); valueIt != target.values.constEnd(); ++valueIt) {
        if (valueIt.key().compare(key, Qt::CaseInsensitive) == 0)
            return valueIt.value();
    }
    return defaultValue;
}

const rfv::IniFile::Section* rfv::IniFile::section(const QString& name) const
{
    const auto it = m_index.constFind(name);
    if (it == m_index.constEnd())
        return nullptr;
    return &m_sections.at(*it);
}
