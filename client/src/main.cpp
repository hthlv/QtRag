#include <QApplication>
#include <QMessageBox>
#include "ui/main_window.h"
#include "storage/database_manager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 客户端启动时先初始化本地数据库，后续会话和配置都依赖它。
    DatabaseManager dbManager("qtrag_client.db");
    if (!dbManager.open() || !dbManager.initializeSchema()) {
        QMessageBox::critical(nullptr, "Database Error", "Failed to initialize client database.");
        return 1;
    }

    // 数据库准备完成后再展示主界面，避免界面起来后才发现基础依赖不可用。
    MainWindow window;
    window.show();

    return app.exec();
}
