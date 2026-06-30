cmake_minimum_required(VERSION 3.14)
project(MiniKV VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

# 跳表库
add_library(skiplist
    src/skiplist/skiplist.cpp
)
target_include_directories(skiplist PUBLIC include)

# 测试可执行文件
add_executable(skiplist_test
    tests/skiplist_test.cpp
)
target_link_libraries(skiplist_test PRIVATE skiplist)