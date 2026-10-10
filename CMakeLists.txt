cmake_minimum_required(VERSION 3.20)

project(BalaMart)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon CONFIG REQUIRED)

add_executable(BalaMart
    backend/main.cpp
)

target_link_libraries(BalaMart PRIVATE Drogon::Drogon)
target_include_directories(BalaMart PRIVATE "C:/Program Files/PostgreSQL/18/include")

target_link_libraries(BalaMart PRIVATE "C:/Program Files/PostgreSQL/18/lib/libpq.lib")