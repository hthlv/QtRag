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

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

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
)

set_target_properties(${QTRAG_CLIENT_TARGET} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${QTRAG_CLIENT_TARGET})
endif()
