cmake_minimum_required(VERSION 3.21)
project(vostop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup()

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(vostop
    src/main.cpp
    src/collect/tools_qt.h
    src/collect/tools_qt.cpp
    src/collect/cpu_mem.h
    src/collect/cpu_mem.cpp
    src/collect/psi.h
    src/collect/psi.cpp
    src/collect/proc.h
    src/collect/proc.cpp
    src/collect/disk_net.h
    src/collect/disk_net.cpp
    src/collect/gpu_sensors.h
    src/collect/gpu_sensors.cpp
    src/collect/gpu_procs.h
    src/collect/gpu_procs.cpp
    src/backend/snapshots.h
    src/backend/Settings.h
    src/backend/Settings.cpp
    src/backend/CollectorWorker.h
    src/backend/CollectorWorker.cpp
    src/backend/Monitors.h
    src/backend/Monitors.cpp
    src/backend/DiskNetMonitors.h
    src/backend/DiskNetMonitors.cpp
    src/backend/GpuSensorsMonitors.h
    src/backend/GpuSensorsMonitors.cpp
    src/backend/ProcessModel.h
    src/backend/ProcessModel.cpp
    src/backend/ProcFilterProxyModel.h
    src/backend/ProcFilterProxyModel.cpp
    src/backend/ProcGrouped.h
    src/backend/ProcGrouped.cpp
    src/backend/Theme.h
    src/backend/Theme.cpp
    src/backend/PanelLayout.h
    src/backend/PanelLayout.cpp
)

set(VOSTOP_PANELS_DIR "${CMAKE_INSTALL_PREFIX}/share/vostop/panels" CACHE STRING "Directory for built-in panels")

qt_add_qml_module(vostop
    URI Vostop
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/PanelLayout.qml
        qml/VostopPanel.qml
        qml/HistoryGraph.qml
)

target_link_libraries(vostop PRIVATE Qt6::Quick)

target_compile_definitions(vostop PRIVATE VOSTOP_PANELS_DIR="${VOSTOP_PANELS_DIR}")

include(GNUInstallDirs)
install(TARGETS vostop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY panels/ DESTINATION share/vostop/panels FILES_MATCHING PATTERN "*.qml")

target_include_directories(vostop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collect
)