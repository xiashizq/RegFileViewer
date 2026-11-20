#pragma once

#include <QHash>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

namespace rfv {

enum class Layout
{
    Ofd,
    Fixed,
    Delimited
};

struct FieldDefinition
{
    int index = 0;              // 1 开始的字段序号
    QString nameCn;             // 中文字段名
    QString nameEn;             // 英文字段名
    QChar type = QLatin1Char('C'); // C 字符 / A 数字字符 / N 数值
    int length = 0;             // 长度（字节或字符，取决于 InterfaceDefinition::lengthType）
    int decimals = 0;           // 小数位数
    int offset = 0;             // 在整行中的起始位置
    QString tip;                // 字段提示
};

using FieldList = QVector<FieldDefinition>;
using FieldDictionary = QHash<QString, QString>;             // 枚举值 -> 含义
using FieldDictionaryMap = QHash<QString, FieldDictionary>;  // 字段名 -> 字典
using FieldDictionaryPtr = QSharedPointer<const FieldDictionaryMap>;

struct InterfaceDefinition
{
    Layout layout = Layout::Ofd;
    QString configFile;          // 例如 OFD_22.ini / FIXED_WDEP_..._V1.1.ini
    QString section;             // 配置段名，例如 01_001_FUND
    QString sectionSuffix;       // 段名中文件类型之后的部分，例如 001_FUND
    QString fileType;            // 文件类型代码，例如 01 / C1 / R1
    QString filePattern;         // 定长文件匹配的通配符
    QString version;             // OFD: 22 / 21 / 20；定长: 1.1
    QString versionLabel;        // 展示用：V2.2 / V1.1
    QString describe;            // 接口说明
    QString protocol;            // 所属协议名称
    QString encoding = QStringLiteral("GB18030");
    QString delimiter = QStringLiteral("|");   // 分隔符文件使用

    QString firstRowCheck;       // 首行标记
    QString lastRowCheck;        // 结束行标记

    int fieldCount = 0;
    int rowLength = 0;
    int lengthType = 0;          // 0=按字节，1=按字符
    int dataBeginRow = 0;        // 数据起始行（1 开始）
    int endIgnoreRows = 0;
    int rowCountCheckRow = 0;    // 记录数所在行（1 开始，0 表示无）
    int dataRecordCountRow = 0;  // OFD 字段名后、数据前的记录数行（1 开始，0 表示无）
    int rowCountLength = 8;      // OFD 记录数字段宽度
    int fieldCountCheckRow = 0;  // 字段总数所在行
    int fieldDetailBeginRow = 0; // 字段明细起始行
    int versionCheckRow = 0;
    QString versionCheckValue;
    int versionCheckMode = 0;

    QString dictionaryGroup;
    FieldDictionaryPtr dictionaries;

    FieldList fields;

    bool usable = true;
    QString problem;

    bool isOfd() const { return layout == Layout::Ofd; }
    bool isDelimited() const { return layout == Layout::Delimited; }

    /// 文件名是否匹配本方案的 filePattern（? 匹配一个字符，* 匹配任意字符）
    bool matchesFileName(const QString& fileName) const;

    /// 例如：开放式基金-账户类申请（01_001_FUND）
    QString title() const;
    QString shortTitle() const;
};

/// 按照接口配置规范化字段值：
/// - C/A 去掉右侧填充空格
/// - N 去掉整数部分前导 0，并按小数位插入小数点
QString formatFieldValue(const QString& raw, const FieldDefinition& field);

/// 去掉前导 V、首尾空白并转大写
QString normalizeVersion(const QString& version);

/// 仅保留字母数字，用于 OFD 版本号比较（2.2 -> 22，V2.2 -> 22）
QString compactVersion(const QString& version);

QString layoutLabel(Layout layout);

} // namespace rfv
