#include "core/encoding.h"

#ifdef RFV_HAVE_TEXTCODEC
#  include <QTextCodec>
#endif

#include <cstring>
#include <string>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

QString normalized(const QString& encoding)
{
    QString name = encoding.trimmed().toUpper();
    name.remove(QLatin1Char('-'));
    name.remove(QLatin1Char('_'));
    name.remove(QLatin1Char(' '));
    if (name.isEmpty())
        return QStringLiteral("UTF-8");
    if (name == QLatin1String("GB18030") || name == QLatin1String("GBK")
        || name == QLatin1String("GB2312") || name == QLatin1String("CP936")
        || name == QLatin1String("GB")) {
        return QStringLiteral("GB18030");
    }
    if (name == QLatin1String("UTF8"))
        return QStringLiteral("UTF-8");
    if (name == QLatin1String("UTF16") || name == QLatin1String("UTF16LE"))
        return QStringLiteral("UTF-16LE");
    if (name == QLatin1String("LATIN1") || name == QLatin1String("ISO88591"))
        return QStringLiteral("ISO-8859-1");
    return name;
}

#ifdef RFV_HAVE_TEXTCODEC
QTextCodec* textCodecFor(const QString& normalizedName)
{
    return QTextCodec::codecForName(normalizedName.toLatin1());
}
#endif

#ifdef Q_OS_WIN
QString decodeGb18030Win32(const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return QString();
    const int length = ::MultiByteToWideChar(54936 /*GB18030*/, 0,
                                             bytes.constData(), bytes.size(),
                                             nullptr, 0);
    if (length <= 0)
        return QString::fromLocal8Bit(bytes);
    std::wstring buffer(static_cast<size_t>(length), L'\0');
    const int converted = ::MultiByteToWideChar(54936, 0, bytes.constData(), bytes.size(),
                                                buffer.data(), length);
    if (converted <= 0)
        return QString::fromLocal8Bit(bytes);
    return QString::fromWCharArray(buffer.data(), converted);
}

QByteArray encodeGb18030Win32(const QString& text)
{
    if (text.isEmpty())
        return QByteArray();
    const int length = ::WideCharToMultiByte(54936, 0,
                                             reinterpret_cast<LPCWSTR>(text.utf16()),
                                             text.size(), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return text.toLocal8Bit();
    QByteArray buffer(length, '\0');
    ::WideCharToMultiByte(54936, 0, reinterpret_cast<LPCWSTR>(text.utf16()),
                          text.size(), buffer.data(), length, nullptr, nullptr);
    return buffer;
}
#endif

} // namespace

QString rfv::normalizeEncoding(const QString& encoding)
{
    return normalized(encoding);
}

QString rfv::decodeText(const QByteArray& bytes, const QString& encoding)
{
    if (bytes.isEmpty())
        return QString();

    const QString name = normalized(encoding);

    if (name == QLatin1String("UTF-8"))
        return QString::fromUtf8(bytes);

    if (name == QLatin1String("UTF-16LE")) {
        return QString::fromUtf16(reinterpret_cast<const ushort*>(bytes.constData()),
                                  bytes.size() / 2);
    }

    if (name == QLatin1String("GB18030")) {
#ifdef RFV_HAVE_TEXTCODEC
        static QTextCodec* const gbCodec = QTextCodec::codecForName("GB18030");
        if (gbCodec)
            return gbCodec->toUnicode(bytes);
#endif
#ifdef Q_OS_WIN
        return decodeGb18030Win32(bytes);
#else
        return QString::fromLocal8Bit(bytes);
#endif
    }

#ifdef RFV_HAVE_TEXTCODEC
    if (QTextCodec* codec = textCodecFor(name))
        return codec->toUnicode(bytes);
#endif

    if (name == QLatin1String("ISO-8859-1"))
        return QString::fromLatin1(bytes);

    // 未知编码：先按 UTF-8 尝试，失败则按本地编码
    const QString utf8 = QString::fromUtf8(bytes);
    if (!utf8.contains(QChar::ReplacementCharacter))
        return utf8;
    return QString::fromLocal8Bit(bytes);
}

QByteArray rfv::encodeText(const QString& text, const QString& encoding)
{
    if (text.isEmpty())
        return QByteArray();

    const QString name = normalized(encoding);

    if (name == QLatin1String("UTF-8"))
        return text.toUtf8();

    if (name == QLatin1String("UTF-16LE")) {
        QByteArray data(text.size() * 2, '\0');
        std::memcpy(data.data(), text.utf16(), static_cast<size_t>(data.size()));
        return data;
    }

    if (name == QLatin1String("GB18030")) {
#ifdef RFV_HAVE_TEXTCODEC
        static QTextCodec* const gbCodec = QTextCodec::codecForName("GB18030");
        if (gbCodec)
            return gbCodec->fromUnicode(text);
#endif
#ifdef Q_OS_WIN
        return encodeGb18030Win32(text);
#else
        return text.toLocal8Bit();
#endif
    }

#ifdef RFV_HAVE_TEXTCODEC
    if (QTextCodec* codec = textCodecFor(name))
        return codec->fromUnicode(text);
#endif

    if (name == QLatin1String("ISO-8859-1"))
        return text.toLatin1();

    return text.toLocal8Bit();
}
