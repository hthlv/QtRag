#include <QApplication>
#include <QMessageBox>
#include "ui/main_window.h"
#include "storage/database_manager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    DatabaseManager dbManager("qtrag_client.db");
    if (!dbManager.open() || !dbManager.initializeSchema()) {
        QMessageBox::critical(nullptr, "Database Error", "Failed to initialize client database.");
        return 1;
    }

    MainWindow window;
    window.show();

    return app.exec();
}
