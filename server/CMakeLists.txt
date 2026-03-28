cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(QtRagServer LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL QUIET)

set(QTRAG_SERVER_TARGET QtRagServer)

add_executable(${QTRAG_SERVER_TARGET}
    src/main.cpp
)

add_subdirectory(src)

target_include_directories(${QTRAG_SERVER_TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${QTRAG_SERVER_TARGET}
    PRIVATE
        Boost::system
        Threads::Threads
        SQLite::SQLite3
        nlohmann_json::nlohmann_json
)

if(OpenSSL_FOUND)
    target_link_libraries(${QTRAG_SERVER_TARGET}
        PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
    )
    target_compile_definitions(${QTRAG_SERVER_TARGET}
        PRIVATE
            QTRAG_SERVER_HAS_OPENSSL=1
    )
endif()
