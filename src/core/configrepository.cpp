#include "core/configrepository.h"

#include "core/encoding.h"
#include "core/inifile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

bool isReservedSection(const QString& name)
{
    return name == QLatin1String("AuthorInformation")
            || name == QLatin1String("TipDictionary")
            || name == QLatin1String("Dictionary")
            || name == QLatin1String("Global")
            || name == QLatin1String("Field");
}

rfv::FieldDictionary parseDictionaryBody(const QString& body)
{
    rfv::FieldDictionary map;
    const QStringList entries = body.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        const int colon = entry.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString code = entry.left(colon).trimmed();
        const QString meaning = entry.mid(colon + 1).trimmed();
        if (!code.isEmpty())
            map.insert(code, meaning);
    }
    return map;
}

QString versionLabelFor(rfv::Layout layout, const QString& version)
{
    const QString normalized = rfv::normalizeVersion(version)
            .remove(QLatin1Char('.'));
    if (layout == rfv::Layout::Ofd && normalized.size() == 2) {
        return QStringLiteral("V%1.%2").arg(normalized.at(0)).arg(normalized.at(1));
    }
    return QStringLiteral("V") + version;
}

QString upperTrimmed(const QString& value)
{
    return value.trimmed().toUpper();
}

} // namespace

QString rfv::protocolNameForConfig(const QString& fileName)
{
    if (fileName.startsWith(QLatin1String("OFD_22")))
        return QStringLiteral("中央数据交换平台开放式基金业务数据交换协议");
    if (fileName.startsWith(QLatin1String("OFD_21")))
        return QStringLiteral("中央数据交换平台开放式基金业务数据交换协议");
    if (fileName.startsWith(QLatin1String("OFD_20")))
        return QStringLiteral("中央数据交换平台开放式基金业务数据交换协议");
    if (fileName.startsWith(QLatin1String("OFD_12"))
        || fileName.startsWith(QLatin1String("OFD_11"))
        || fileName.startsWith(QLatin1String("OFD_10"))) {
        return QStringLiteral("中国结算开放式基金新版系统管理人数据接口规范");
    }
    if (fileName.startsWith(QLatin1String("OFD_ZBD")))
        return QStringLiteral("中登电子合同数据接口");
    if (fileName.startsWith(QLatin1String("FIXED_WDEP")))
        return QStringLiteral("理财产品中央数据交换平台数据交换协议");
    if (fileName.startsWith(QLatin1String("FIXED_WPID")))
        return QStringLiteral("理财登信息披露数据接口");
    if (fileName.contains(QLatin1String("理财")))
        return QStringLiteral("理财登记相关接口");
    if (fileName.startsWith(QLatin1String("OFD_")))
        return QStringLiteral("OFD 定长接口");
    return QStringLiteral("字段定长接口");
}

QString rfv::ConfigRepository::locateConfDir()
{
    QStringList candidates;

    const QString envDir = QProcessEnvironment::systemEnvironment()
            .value(QStringLiteral("RFV_CONF_DIR"));
    if (!envDir.isEmpty())
        candidates.append(envDir);

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates.append(appDir + QStringLiteral("/conf"));
    candidates.append(appDir + QStringLiteral("/../conf"));
    candidates.append(appDir + QStringLiteral("/../../conf"));

#ifdef RFV_SOURCE_CONF
    candidates.append(QStringLiteral(RFV_SOURCE_CONF));
#endif

    candidates.append(QDir::currentPath() + QStringLiteral("/conf"));

    const QStringList patterns = {QStringLiteral("OFD_*.ini"),
                                  QStringLiteral("FIXED_*.ini")};

    for (const QString& candidate : candidates) {
        QDir dir(candidate);
        if (!dir.exists())
            continue;
        for (const QString& pattern : patterns) {
            if (!dir.entryList(QStringList() << pattern, QDir::Files).isEmpty())
                return dir.absolutePath();
        }
    }
    return QString();
}

bool rfv::ConfigRepository::load(const QString& confDir, QStringList* messages)
{
    m_confDir = confDir;
    m_ofd.clear();
    m_fixed.clear();
    m_ofdDictionaries.clear();
    m_codes.clear();
    m_warnings.clear();

    QDir dir(confDir);
    if (!dir.exists()) {
        m_warnings.append(QStringLiteral("配置目录不存在：%1").arg(confDir));
        if (messages)
            *messages = m_warnings;
        return false;
    }

    loadDictionaries(confDir + QStringLiteral("/OFD_Dictionary.ini"));
    loadCodes(confDir + QStringLiteral("/OFD_CodeInfo.ini"));

    const QStringList ofdFiles = dir.entryList(QStringList() << QStringLiteral("OFD_*.ini"),
                                               QDir::Files, QDir::Name);
    for (const QString& fileName : ofdFiles) {
        if (fileName == QLatin1String("OFD_Dictionary.ini")
            || fileName == QLatin1String("OFD_CodeInfo.ini")
            || fileName == QLatin1String("OFD_IndexFile.ini")
            || fileName == QLatin1String("OFD_Like.ini")
            || fileName == QLatin1String("OFD_TipDictionary.ini")) {
            continue;
        }
        loadOfdFile(confDir + QLatin1Char('/') + fileName);
    }

    const QStringList fixedFiles = dir.entryList(QStringList() << QStringLiteral("FIXED_*.ini"),
                                                 QDir::Files, QDir::Name);
    for (const QString& fileName : fixedFiles)
        loadFixedFile(confDir + QLatin1Char('/') + fileName);

    if (m_ofd.isEmpty() && m_fixed.isEmpty())
        m_warnings.append(QStringLiteral("未能加载任何解析配置，请检查 conf 目录"));

    if (messages)
        *messages = m_warnings;
    return !m_ofd.isEmpty() || !m_fixed.isEmpty();
}

void rfv::ConfigRepository::loadDictionaries(const QString& path)
{
    if (!QFileInfo::exists(path))
        return;

    IniFile ini;
    QString error;
    if (!ini.load(path, &error)) {
        m_warnings.append(error);
        return;
    }

    for (const IniFile::Section& section : ini.sections()) {
        if (isReservedSection(section.name))
            continue;
        QSharedPointer<FieldDictionaryMap> map =
                QSharedPointer<FieldDictionaryMap>::create();
        for (const QString& key : section.keyOrder)
            map->insert(key, parseDictionaryBody(section.values.value(key)));
        if (!map->isEmpty())
            m_ofdDictionaries.insert(section.name, map);
    }
}

void rfv::ConfigRepository::loadCodes(const QString& path)
{
    if (!QFileInfo::exists(path))
        return;

    IniFile ini;
    QString error;
    if (!ini.load(path, &error)) {
        m_warnings.append(error);
        return;
    }

    for (const IniFile::Section& section : ini.sections()) {
        const QString name = ini.value(section.name, QStringLiteral("NAME"));
        if (!name.isEmpty())
            m_codes.insert(section.name.trimmed().toUpper(), name);
    }
}

void rfv::ConfigRepository::applyFieldTips(InterfaceDefinition& def, const IniFile& ini) const
{
    const IniFile::Section* tips = ini.section(QStringLiteral("TipDictionary"));
    if (!tips)
        return;

    for (const QString& key : tips->keyOrder) {
        const QString text = tips->values.value(key);
        if (text.isEmpty())
            continue;

        bool ok = false;
        const int index = key.trimmed().toInt(&ok);
        if (ok && index >= 1 && index <= def.fields.size())
            def.fields[index - 1].tip = text;
    }
}

void rfv::ConfigRepository::loadOfdFile(const QString& path)
{
    const QString fileName = QFileInfo(path).fileName();

    QRegularExpression versionRegex(QStringLiteral("^OFD_(.+)\\.ini$"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = versionRegex.match(fileName);
    if (!match.hasMatch())
        return;
    const QString fileVersion = normalizeVersion(match.captured(1));

    IniFile ini;
    QString error;
    if (!ini.load(path, &error)) {
        m_warnings.append(error);
        return;
    }

    for (const IniFile::Section& section : ini.sections()) {
        if (isReservedSection(section.name))
            continue;

        const QString sectionName = section.name;
        const int underscore = sectionName.indexOf(QLatin1Char('_'));

        InterfaceDefinition def;
        def.layout = Layout::Ofd;
        def.configFile = fileName;
        def.section = sectionName;
        def.fileType = underscore > 0 ? sectionName.left(underscore) : sectionName;
        def.sectionSuffix = underscore > 0 ? sectionName.mid(underscore + 1) : QString();
        def.version = fileVersion;
        def.versionLabel = versionLabelFor(Layout::Ofd, fileVersion);
        def.protocol = protocolNameForConfig(fileName);
        def.describe = ini.value(sectionName, QStringLiteral("DESCRIBE"));
        if (def.describe.isEmpty())
            def.describe = sectionName;
        def.encoding = ini.value(sectionName, QStringLiteral("ENCODING"),
                                 QStringLiteral("GB18030"));
        def.firstRowCheck = QStringLiteral("OFDCFDAT");
        def.lastRowCheck = QStringLiteral("OFDCFEND");
        def.versionCheckRow = 2;
        def.versionCheckValue = fileVersion;
        def.rowCountLength = ini.value(sectionName, QStringLiteral("ROWCOUNTLENGTH"))
                .toInt();
        if (def.rowCountLength <= 0)
            def.rowCountLength = 8;

        def.dictionaryGroup = ini.value(sectionName, QStringLiteral("DICTIONARY"));
        if (def.dictionaryGroup.isEmpty() && def.sectionSuffix.size() >= 3)
            def.dictionaryGroup = def.sectionSuffix.left(3);
        def.dictionaries = m_ofdDictionaries.value(def.dictionaryGroup);

        bool countOk = false;
        const int fieldCount = ini.value(sectionName, QStringLiteral("COUNT"))
                .toInt(&countOk);

        if (!countOk || fieldCount <= 0) {
            def.usable = false;
            def.problem = QStringLiteral("COUNT 配置无效");
            m_ofd.append(def);
            continue;
        }

        def.fieldCount = fieldCount;

        int offset = 0;
        bool fieldsOk = true;
        for (int i = 1; i <= fieldCount; ++i) {
            const QString raw = ini.value(sectionName, QString::number(i));
            const QStringList parts = raw.split(QLatin1Char(','));
            if (parts.size() < 4 || parts.at(1).trimmed().toInt() <= 0) {
                fieldsOk = false;
                def.problem = QStringLiteral("第 %1 个字段定义无效：%2").arg(i).arg(raw);
                break;
            }

            FieldDefinition field;
            field.index = i;
            field.type = parts.at(0).trimmed().isEmpty()
                    ? QLatin1Char('C') : parts.at(0).trimmed().at(0).toUpper();
            field.length = parts.at(1).trimmed().toInt();
            field.decimals = parts.at(2).trimmed().toInt();
            field.nameCn = parts.at(3).trimmed();
            field.nameEn = parts.size() > 4 ? parts.at(4).trimmed() : QString();
            field.offset = offset;
            offset += field.length;
            def.fields.append(field);
        }

        if (!fieldsOk) {
            def.usable = false;
            m_ofd.append(def);
            continue;
        }

        def.rowLength = offset;
        def.dataBeginRow = 11 + fieldCount;
        def.usable = true;
        m_ofd.append(def);
    }
}

void rfv::ConfigRepository::loadFixedFile(const QString& path)
{
    const QString fileName = QFileInfo(path).fileName();

    IniFile ini;
    QString error;
    if (!ini.load(path, &error)) {
        m_warnings.append(error);
        return;
    }

    // 一个配置文件共享一份 [Dictionary] 字典
    QSharedPointer<FieldDictionaryMap> dictionaries =
            QSharedPointer<FieldDictionaryMap>::create();
    if (const IniFile::Section* dictSection = ini.section(QStringLiteral("Dictionary"))) {
        for (const QString& key : dictSection->keyOrder) {
            const QString value = dictSection->values.value(key);
            const int at = value.indexOf(QLatin1Char('@'));
            if (at <= 0)
                continue;
            const QString fieldName = value.left(at).trimmed();
            dictionaries->insert(fieldName, parseDictionaryBody(value.mid(at + 1)));
        }
    }

    for (const IniFile::Section& section : ini.sections()) {
        if (isReservedSection(section.name))
            continue;

        const int bar = section.name.lastIndexOf(QLatin1Char('|'));
        if (bar <= 0)
            continue;

        InterfaceDefinition def;
        def.layout = Layout::Fixed;
        def.configFile = fileName;
        def.section = section.name;
        def.filePattern = section.name.left(bar).trimmed();
        def.version = normalizeVersion(section.name.mid(bar + 1));
        def.versionLabel = versionLabelFor(Layout::Fixed, def.version);
        def.protocol = protocolNameForConfig(fileName);
        def.describe = ini.value(section.name, QStringLiteral("filedescribe"));
        if (def.describe.isEmpty())
            def.describe = section.name;

        QString fieldCountText = ini.value(section.name, QStringLiteral("fieldcount"));
        fieldCountText = fieldCountText.split(QLatin1Char('|')).value(0).trimmed();
        bool countOk = false;
        const int fieldCount = fieldCountText.toInt(&countOk);
        if (!countOk || fieldCount <= 0) {
            def.usable = false;
            def.problem = QStringLiteral("fieldcount 配置无效：%1").arg(fieldCountText);
            m_fixed.append(def);
            continue;
        }
        def.fieldCount = fieldCount;

        def.lengthType = ini.value(section.name, QStringLiteral("fieldlengthtype")).toInt();
        def.encoding = ini.value(section.name, QStringLiteral("encoding"),
                                 QStringLiteral("GB18030"));
        def.firstRowCheck = ini.value(section.name, QStringLiteral("firstrowcheck"));
        def.lastRowCheck = ini.value(section.name, QStringLiteral("lastrowcheck"));
        def.dataBeginRow = ini.value(section.name, QStringLiteral("datarowbeginindex")).toInt();
        if (def.dataBeginRow <= 0)
            def.dataBeginRow = 2;
        def.endIgnoreRows = ini.value(section.name, QStringLiteral("endignorerow")).toInt();
        def.rowCountCheckRow = ini.value(section.name, QStringLiteral("rowcountcheckrow")).toInt();
        def.fieldCountCheckRow = ini.value(section.name, QStringLiteral("fieldcountcheckrow")).toInt();
        def.fieldDetailBeginRow = ini.value(section.name, QStringLiteral("fielddetailcheckbeginrow")).toInt();
        def.versionCheckRow = ini.value(section.name, QStringLiteral("versioncheckrow")).toInt();
        def.versionCheckValue = normalizeVersion(
                    ini.value(section.name, QStringLiteral("version")));
        if (def.versionCheckValue.isEmpty())
            def.versionCheckValue = def.version;
        def.versionCheckMode = ini.value(section.name, QStringLiteral("versioncheckmode")).toInt();
        def.dictionaries = dictionaries;

        int offset = 0;
        bool fieldsOk = true;
        for (int i = 1; i <= fieldCount; ++i) {
            const QString raw = ini.value(section.name, QString::number(i));
            const QStringList parts = raw.split(QLatin1Char(','));
            if (parts.size() < 4 || parts.at(1).trimmed().toInt() <= 0) {
                fieldsOk = false;
                def.problem = QStringLiteral("第 %1 个字段定义无效：%2").arg(i).arg(raw);
                break;
            }

            FieldDefinition field;
            field.index = i;
            field.type = parts.at(0).trimmed().isEmpty()
                    ? QLatin1Char('C') : parts.at(0).trimmed().at(0).toUpper();
            field.length = parts.at(1).trimmed().toInt();
            field.decimals = parts.at(2).trimmed().toInt();
            field.nameCn = parts.at(3).trimmed();
            field.nameEn = parts.size() > 4 ? parts.at(4).trimmed() : QString();
            field.offset = offset;
            offset += field.length;
            def.fields.append(field);
        }

        if (!fieldsOk) {
            def.usable = false;
            m_fixed.append(def);
            continue;
        }

        def.rowLength = offset;
        def.usable = true;
        applyFieldTips(def, ini);
        m_fixed.append(def);
    }
}

QList<rfv::InterfaceDefinition> rfv::ConfigRepository::candidates(
        bool isOfd, const QString& version, const QString& fileType,
        const QString& fileName, const QString& firstLine) const
{
    QList<InterfaceDefinition> result;

    if (isOfd) {
        const QString wantedVersion = compactVersion(version);
        const QString wantedType = upperTrimmed(fileType);

        for (const InterfaceDefinition& def : m_ofd) {
            if (!def.usable)
                continue;
            if (compactVersion(def.version) != wantedVersion)
                continue;
            if (!def.fileType.isEmpty() && upperTrimmed(def.fileType) != wantedType)
                continue;
            result.append(def);
        }

        std::stable_sort(result.begin(), result.end(),
                         [](const InterfaceDefinition& a, const InterfaceDefinition& b) {
            auto score = [](const InterfaceDefinition& d) {
                if (d.sectionSuffix.compare(QLatin1String("001_FUND"),
                                             Qt::CaseInsensitive) == 0)
                    return 0;
                if (d.sectionSuffix.startsWith(QLatin1String("001")))
                    return 1;
                if (d.sectionSuffix.startsWith(QLatin1String("003")))
                    return 2;
                return 3;
            };
            const int sa = score(a);
            const int sb = score(b);
            if (sa != sb)
                return sa < sb;
            return a.section < b.section;
        });
        return result;
    }

    const QString wantedVersion = normalizeVersion(version);
    const QString wantedFirst = upperTrimmed(firstLine);

    auto matchesFirstLine = [&](const InterfaceDefinition& def) {
        if (def.firstRowCheck.isEmpty())
            return true;
        return upperTrimmed(def.firstRowCheck) == wantedFirst;
    };
    auto matchesVersion = [&](const InterfaceDefinition& def) {
        if (wantedVersion.isEmpty())
            return true;
        return normalizeVersion(def.version) == wantedVersion;
    };

    QSet<QString> seen;
    auto appendIfNew = [&](const InterfaceDefinition& def) {
        const QString key = def.configFile + QLatin1Char('|') + def.section;
        if (seen.contains(key))
            return;
        seen.insert(key);
        result.append(def);
    };

    // 第一轮：文件名 + 版本 + 首行全部匹配
    for (const InterfaceDefinition& def : m_fixed) {
        if (def.usable && def.matchesFileName(fileName)
            && matchesVersion(def) && matchesFirstLine(def)) {
            appendIfNew(def);
        }
    }
    // 第二轮：文件名 + 首行（忽略版本）
    if (result.isEmpty()) {
        for (const InterfaceDefinition& def : m_fixed) {
            if (def.usable && def.matchesFileName(fileName) && matchesFirstLine(def))
                appendIfNew(def);
        }
    }
    // 第三轮：版本 + 首行（文件名可能被改过）
    if (result.isEmpty()) {
        for (const InterfaceDefinition& def : m_fixed) {
            if (def.usable && matchesVersion(def) && matchesFirstLine(def))
                appendIfNew(def);
        }
    }
    // 第四轮：仅首行标记
    if (result.isEmpty()) {
        for (const InterfaceDefinition& def : m_fixed) {
            if (def.usable && matchesFirstLine(def))
                appendIfNew(def);
        }
    }

    return result;
}

QString rfv::ConfigRepository::codeName(const QString& code) const
{
    const QString key = code.trimmed().toUpper();
    return m_codes.value(key);
}

int rfv::headerFieldMatchCount(const InterfaceDefinition& definition,
                               const QStringList& headerFieldNames)
{
    if (headerFieldNames.isEmpty() || definition.fields.isEmpty())
        return 0;

    const int count = qMin(headerFieldNames.size(), definition.fields.size());
    int matched = 0;
    for (int i = 0; i < count; ++i) {
        const QString fileField = headerFieldNames.at(i).trimmed();
        const QString configField = definition.fields.at(i).nameEn.trimmed();
        if (!fileField.isEmpty() && !configField.isEmpty()
            && fileField.compare(configField, Qt::CaseInsensitive) == 0) {
            ++matched;
        }
    }
    return matched;
}
