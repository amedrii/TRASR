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
    screen_completion.cpp
    run_monitor.h
    screen_completion.h
    trasr_window.h
    tra_process.h
    run_rules.h
)

target_link_libraries(TRASR PRIVATE Qt6::Widgets Qt6::Network)
add_custom_command(TARGET TRASR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TRASR>/tools"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/level_end_detector.py"
        "$<TARGET_FILE_DIR:TRASR>/tools/level_end_detector.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/requirements-level-end-detector.txt"
        "$<TARGET_FILE_DIR:TRASR>/tools/requirements-level-end-detector.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/menu_navigator.py"
        "$<TARGET_FILE_DIR:TRASR>/tools/menu_navigator.py"
)