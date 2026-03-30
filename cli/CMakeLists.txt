cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(QtRagCli LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QTRAG_CLI_QT_MAJOR "auto" CACHE STRING "Qt major version for cli: auto, 5, or 6")
set_property(CACHE QTRAG_CLI_QT_MAJOR PROPERTY STRINGS auto 5 6)

if(QTRAG_CLI_QT_MAJOR STREQUAL "5")
    set(_qtrag_cli_qt_candidates Qt5)
elseif(QTRAG_CLI_QT_MAJOR STREQUAL "6")
    set(_qtrag_cli_qt_candidates Qt6)
else()
    set(_qtrag_cli_qt_candidates Qt6 Qt5)
endif()

find_package(QT NAMES ${_qtrag_cli_qt_candidates} REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

set(QTRAG_CLI_TARGET QtRagCli)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(${QTRAG_CLI_TARGET}
        MANUAL_FINALIZATION
        src/main.cpp
    )
else()
    add_executable(${QTRAG_CLI_TARGET} src/main.cpp)
endif()

target_link_libraries(${QTRAG_CLI_TARGET}
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
)

set_target_properties(${QTRAG_CLI_TARGET} PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${QTRAG_CLI_TARGET})
endif()
