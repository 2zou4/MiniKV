cmake_minimum_required(VERSION 3.14)
project(MiniKV VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

# ── 跳表库 ──
add_library(skiplist
    src/skiplist/skiplist.cpp
)
target_include_directories(skiplist PUBLIC include)

# ── WAL 库 ──
add_library(wal
    src/wal/wal.cpp
)
target_include_directories(wal PUBLIC include)

# ── 测试可执行文件 ──
add_executable(skiplist_test
    tests/skiplist_test.cpp
)
target_link_libraries(skiplist_test PRIVATE skiplist)

add_executable(wal_test
    tests/wal_test.cpp
)
target_link_libraries(wal_test PRIVATE wal)

# ── MemTable 库 ──
add_library(memtable
    src/memtable/memtable.cpp
)
target_include_directories(memtable PUBLIC include)
target_link_libraries(memtable PUBLIC skiplist wal)

add_executable(memtable_test
    tests/memtable_test.cpp
)
target_link_libraries(memtable_test PRIVATE memtable)

# ── SSTable 库（含 Bloom Filter）──
add_library(sstable
    src/sstable/sstable.cpp
    src/sstable/bloom_filter.cpp
)
target_include_directories(sstable PUBLIC include)
target_link_libraries(sstable PUBLIC skiplist)

add_executable(bloom_filter_test
    tests/bloom_filter_test.cpp
)
target_link_libraries(bloom_filter_test PRIVATE sstable)

add_executable(sstable_test
    tests/sstable_test.cpp
)
target_link_libraries(sstable_test PRIVATE sstable)

add_library(compaction
    src/compaction/compaction.cpp
)
target_include_directories(compaction PUBLIC include)

add_executable(compaction_test
    tests/compaction_test.cpp
)
target_link_libraries(compaction_test PRIVATE compaction)