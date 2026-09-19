cmake_minimum_required(VERSION 3.16)

project(TRASR VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

qt_add_executable(TRASR
    main.cpp
)

target_link_libraries(TRASR PRIVATE Qt6::Widgets Qt6::Network)