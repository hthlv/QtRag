#include "http/http_server.h"
#include "storage/sqllite_store.h"
#include <iostream>

int main()
{
    try {
        // 服务启动时先准备数据库，再开始监听 HTTP 端口。
        SqlliteStore store("qtrag_server.db");
        store.open();
        store.initialize_schema();

        HttpServer server("127.0.0.1", 8080, store.db());
        server.run();
    } catch (const std::exception& e) {
        // 启动阶段出现任何异常都直接终止进程，避免服务处于半可用状态。
        std::cerr << "[Fatal Error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
