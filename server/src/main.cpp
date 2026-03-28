#include "config/app_config.h"
#include "http/http_server.h"
#include "storage/sqllite_store.h"
#include "utils/logger.h"

int main() {
    try {
        // 1. 读取配置
        AppConfig config = AppConfig::load_from_file("config/config.openai.example.json");
        // 2. 初始化 SQLite
        // 服务启动时先准备数据库，再开始监听 HTTP 端口。
        SqlliteStore store("qtrag_server.db");
        store.open();
        store.initialize_schema();
        // 3. 启动服务端
        HttpServer server(config, store.db());
        // 4. 启动前恢复持久化索引
        server.initialize_index_from_storage();
        // 5. 运行服务
        server.run();
    } catch (const std::exception &e) {
        // 启动阶段出现任何异常都直接终止进程，避免服务处于半可用状态。
        log_fatal("main", e.what());
        return 1;
    }

    return 0;
}
