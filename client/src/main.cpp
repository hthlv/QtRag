#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QtGlobal>
#include "ui/main_window.h"
#include "ui/app_style.h"
#include "storage/database_manager.h"

namespace {

QString qtPluginsPath()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QLibraryInfo::path(QLibraryInfo::PluginsPath);
#else
    return QLibraryInfo::location(QLibraryInfo::PluginsPath);
#endif
}

bool inputContextPluginExists(const QString &pluginName)
{
    const QString pluginDir = qtPluginsPath() + "/platforminputcontexts";
    const QDir dir(pluginDir);
    if (!dir.exists()) {
        return false;
    }
    const QStringList entries = dir.entryList(QDir::Files);
    for (const QString &entry : entries) {
        if (entry.contains(pluginName, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void normalizeQtInputModule()
{
#if defined(Q_OS_LINUX)
    const QByteArray requestedModule = qgetenv("QT_IM_MODULE").trimmed();
    if (requestedModule.isEmpty()) {
        return;
    }
    if (inputContextPluginExists(QString::fromUtf8(requestedModule))) {
        return;
    }

    // Qt6 在没有对应插件时会退回到 compose，中文输入通常会失效。
    // 如果系统存在 XIM 或 ibus 路径，这里主动切过去。
    if (!QString::fromLocal8Bit(qgetenv("XMODIFIERS")).trimmed().isEmpty()
        && inputContextPluginExists("compose")) {
        qputenv("QT_IM_MODULE", "xim");
        return;
    }
    if (inputContextPluginExists("ibus")) {
        qputenv("QT_IM_MODULE", "ibus");
        return;
    }
    qputenv("QT_IM_MODULE", "compose");
#endif
}

}

int main(int argc, char *argv[])
{
    normalizeQtInputModule();
    QApplication app(argc, argv);
    const QIcon appIcon(QStringLiteral(":/icons/qtrag-logo.svg"));
    app.setWindowIcon(appIcon);
    applyAppStyle(&app);

    // 客户端启动时先初始化本地数据库，后续会话和配置都依赖它。
    DatabaseManager dbManager("qtrag_client.db");
    if (!dbManager.open() || !dbManager.initializeSchema()) {
        QMessageBox::critical(nullptr, "Database Error", "Failed to initialize client database.");
        return 1;
    }

    // 数据库准备完成后再展示主界面，避免界面起来后才发现基础依赖不可用。
    MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();
    app.processEvents();

    return app.exec();
}
