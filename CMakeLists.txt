cmake_minimum_required(VERSION 3.21)
project(vostop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup()

qt_policy(SET QTP0001 NEW)

qt_add_executable(vostop
    src/main.cpp
)

qt_add_qml_module(vostop
    URI Vostop
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/CpuCard.qml
        qml/MemCard.qml
        qml/DiskCard.qml
        qml/NetCard.qml
        qml/ProcessList.qml
        qml/GpuCard.qml
        qml/TempCard.qml
        qml/HistoryGraph.qml
)

target_link_libraries(vostop PRIVATE Qt6::Quick)