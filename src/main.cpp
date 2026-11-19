#include "core/configrepository.h"
#include "gui/mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStringList>

namespace {

QIcon createApplicationIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x1f, 0x6f, 0xeb));
    painter.drawRoundedRect(QRectF(3, 3, 58, 58), 12, 12);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(28);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("RF"));
    painter.end();

    return QIcon(pixmap);
}

} // namespace

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("RegFileViewer"));
    application.setApplicationName(QStringLiteral("RegFileViewer"));
#ifdef RFV_PROJECT_VERSION
    application.setApplicationVersion(QStringLiteral(RFV_PROJECT_VERSION));
#endif
    application.setWindowIcon(createApplicationIcon());

    // 从旧名称（FinanceFileReader）迁移一次用户设置
    {
        QSettings current;
        QSettings legacy(QStringLiteral("FinanceFileReader"),
                         QStringLiteral("FinanceFileReader"));
        const QStringList keys = {
            QStringLiteral("windowGeometry"),
            QStringLiteral("recentFiles"),
            QStringLiteral("showInfoPanel"),
            QStringLiteral("lastDir")
        };
        for (const QString& key : keys) {
            if (!current.contains(key) && legacy.contains(key))
                current.setValue(key, legacy.value(key));
        }
    }

#ifdef Q_OS_WIN
    QFont font = application.font();
    font.setFamily(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(9);
    application.setFont(font);
#endif

    rfv::ConfigRepository repository;
    const QString confDir = rfv::ConfigRepository::locateConfDir();

    QStringList messages;
    if (confDir.isEmpty() || !repository.load(confDir, &messages)) {
        QString text = QStringLiteral(
            "未能加载接口解析配置。\n\n"
            "请确认程序目录下存在 conf 文件夹（包含 OFD_*.ini / FIXED_*.ini）。\n"
            "也可以通过环境变量 RFV_CONF_DIR 指定配置目录。");
        if (!messages.isEmpty())
            text += QStringLiteral("\n\n详细信息：\n") + messages.join(QLatin1Char('\n'));

        QMessageBox::critical(nullptr, QStringLiteral("配置错误"), text);
        return 1;
    }

    rfv::MainWindow window(&repository);
    window.show();

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
        window.openFile(arguments.at(1));

    return application.exec();
}
