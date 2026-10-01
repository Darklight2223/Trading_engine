cmake_minimum_required(VERSION 3.20)
project(market_microstructure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(trading_core
    src/order_book.cpp
    src/matching_engine.cpp
    src/simulator.cpp
)
target_include_directories(trading_core PUBLIC include)
target_compile_options(trading_core PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

add_executable(trading_demo src/main.cpp)
target_link_libraries(trading_demo PRIVATE trading_core)

add_executable(matching_tests tests/test_matching.cpp)
target_link_libraries(matching_tests PRIVATE trading_core)

enable_testing()
add_test(NAME matching_tests COMMAND matching_tests)
