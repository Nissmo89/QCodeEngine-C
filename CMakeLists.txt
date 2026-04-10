cmake_minimum_required(VERSION 3.16)
project(QCodeEngine-C VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_subdirectory(QCodeEngine_C)

add_executable(QCodeEngine-C
    main.cpp
)

target_link_libraries(QCodeEngine-C PRIVATE QCodeEngine_C Qt6::Widgets)
