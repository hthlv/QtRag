cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(QtRagClient LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QTRAG_CLIENT_QT_MAJOR "auto" CACHE STRING "Qt major version for client: auto, 5, or 6")
set_property(CACHE QTRAG_CLIENT_QT_MAJOR PROPERTY STRINGS auto 5 6)

if(QTRAG_CLIENT_QT_MAJOR STREQUAL "5")
    set(_qtrag_client_qt_candidates Qt5)
elseif(QTRAG_CLIENT_QT_MAJOR STREQUAL "6")
    set(_qtrag_client_qt_candidates Qt6)
else()
    set(_qtrag_client_qt_candidates Qt6 Qt5)
endif()

find_package(QT NAMES ${_qtrag_client_qt_candidates} REQUIRED COMPONENTS Widgets Sql Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql Network)

set(QTRAG_CLIENT_HAS_WEBENGINE OFF)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 QUIET COMPONENTS WebEngineWidgets)
    if(TARGET Qt6::WebEngineWidgets)
        set(QTRAG_CLIENT_HAS_WEBENGINE ON)
    endif()
else()
    find_package(Qt5 QUIET COMPONENTS WebEngineWidgets)
    if(TARGET Qt5::WebEngineWidgets)
        set(QTRAG_CLIENT_HAS_WEBENGINE ON)
    endif()
endif()

set(QTRAG_CLIENT_TARGET QtRagClient)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(${QTRAG_CLIENT_TARGET}
        MANUAL_FINALIZATION
        src/main.cpp
    )
else()
    add_executable(${QTRAG_CLIENT_TARGET} src/main.cpp)
endif()

add_subdirectory(src)

target_include_directories(${QTRAG_CLIENT_TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${QTRAG_CLIENT_TARGET}
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Network
)

if(QTRAG_CLIENT_HAS_WEBENGINE)
    target_link_libraries(${QTRAG_CLIENT_TARGET}
        PRIVATE
            Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    )
    target_compile_definitions(${QTRAG_CLIENT_TARGET}
        PRIVATE
            QTRAG_CLIENT_HAS_WEBENGINE=1
    )
endif()

set_target_properties(${QTRAG_CLIENT_TARGET} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${QTRAG_CLIENT_TARGET})
endif()
