#include "core/interface.h"

#include <QRegularExpression>

namespace rfv {

bool InterfaceDefinition::matchesFileName(const QString& fileName) const
{
    if (filePattern.isEmpty())
        return true;

    QString pattern = QRegularExpression::escape(filePattern);
    pattern.replace(QStringLiteral("\\?"), QStringLiteral("."));
    pattern.replace(QStringLiteral("\\*"), QStringLiteral(".*"));

    QRegularExpression re(QStringLiteral("^") + pattern + QStringLiteral("$"),
                          QRegularExpression::CaseInsensitiveOption);
    return re.match(fileName).hasMatch();
}

QString InterfaceDefinition::title() const
{
    if (section.isEmpty())
        return describe;
    return QStringLiteral("%1（%2）").arg(describe, section);
}

QString InterfaceDefinition::shortTitle() const
{
    return describe.isEmpty() ? section : describe;
}

QString formatFieldValue(const QString& raw, const FieldDefinition& field)
{
    if (field.type != QLatin1Char('N'))
        return raw.trimmed();

    QString value = raw.trimmed();
    if (value.isEmpty())
        return QString();

    bool negative = false;
    if (value.startsWith(QLatin1Char('-'))) {
        negative = true;
        value.remove(0, 1);
    }

    if (field.decimals <= 0) {
        int pos = 0;
        while (pos < value.size() - 1 && value.at(pos) == QLatin1Char('0'))
            ++pos;
        value = value.mid(pos);
    } else {
        const int decimals = field.decimals;
        if (value.size() <= decimals)
            value = QString(decimals + 1 - value.size(), QLatin1Char('0')) + value;

        const int integerLength = value.size() - decimals;
        QString integerPart = value.left(integerLength);
        int pos = 0;
        while (pos < integerPart.size() - 1 && integerPart.at(pos) == QLatin1Char('0'))
            ++pos;
        integerPart = integerPart.mid(pos);

        value = integerPart + QLatin1Char('.') + value.right(decimals);
    }

    return negative ? QLatin1Char('-') + value : value;
}

QString normalizeVersion(const QString& version)
{
    QString value = version.trimmed().toUpper();
    if (value.startsWith(QLatin1Char('V')))
        value.remove(0, 1);
    return value;
}

QString compactVersion(const QString& version)
{
    QString value;
    for (const QChar ch : version) {
        if (ch.isLetterOrNumber())
            value.append(ch.toUpper());
    }
    return value;
}

QString layoutLabel(Layout layout)
{
    switch (layout) {
    case Layout::Ofd:
        return QStringLiteral("OFD 定长文件");
    case Layout::Fixed:
        return QStringLiteral("字段定长文件");
    case Layout::Delimited:
        return QStringLiteral("分隔符文件");
    }
    return QStringLiteral("未知格式");
}

} // namespace rfv
