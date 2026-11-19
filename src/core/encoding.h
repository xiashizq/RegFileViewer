#pragma once

#include <QByteArray>
#include <QString>

namespace rfv {

/// 按接口配置中的编码名解码字节串。
/// 支持 UTF-8 / GB18030(GBK/GB2312 统一映射到 GB18030) / ISO-8859-1 / 系统编码。
QString decodeText(const QByteArray& bytes, const QString& encoding);

/// 按接口配置中的编码把文本编码为字节串（GB18030 / UTF-8 等）。
QByteArray encodeText(const QString& text, const QString& encoding);

/// 把编码名归一化，便于比较。
QString normalizeEncoding(const QString& encoding);

} // namespace rfv
