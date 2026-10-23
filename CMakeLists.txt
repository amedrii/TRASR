cmake_minimum_required(VERSION 3.16)

project(TRASR VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

qt_add_executable(TRASR
    main.cpp
    tra_process.cpp
    run_rules.cpp
    trasr_window.cpp
    run_monitor.cpp
    run_monitor.h
    trasr_window.h
    tra_process.h
    run_rules.h
)

target_link_libraries(TRASR PRIVATE Qt6::Widgets Qt6::Network)