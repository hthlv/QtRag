#include "http/http_server.h"
#include "storage/sqllite_store.h"
#include <iostream>
int main()
{
    try {
        SqlliteStore store("qtrag_server.db");
        store.open();
        store.initialize_schema();
        HttpServer server("127.0.0.1", 8080);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
