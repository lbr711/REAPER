# CMake basics
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
set(CMAKE_C_COMPILER "/usr/local/bin/gcc")
set(CMAKE_CXX_COMPILER "/usr/local/bin/g++")

project(Reaper)
set(CMAKE_CXX_STANDARD 14)

include_directories(/usr/local/include/dpdk)

# Add path_prefix and options for PyTorch C++
set(CMAKE_PREFIX_PATH /home/sdn/lbr/libtorch)

find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

find_package(TBB REQUIRED)

# Add sub-directories
add_subdirectory(runtime)

# Add the traget sorce code files
aux_source_directory(. DIR_SRCS)
add_executable(${PROJECT_NAME} "${DIR_SRCS}")

# Add the libraries dependencies
target_link_libraries(${PROJECT_NAME} gflags)
target_link_libraries(${PROJECT_NAME} runtime)
