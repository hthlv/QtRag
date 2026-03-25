#include "http/http_server.h"
#include <iostream>
int main()
{
    try {
        HttpServer server("127.0.0.1", 8080);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "[Fatal Error] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
