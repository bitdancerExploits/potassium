cmake_minimum_required(VERSION 3.20)
project(ProjectX_EnvironmentBridge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_library(project_x_core STATIC
    src/memory_bridge.cpp
    src/lua_vm_bridge.cpp
    src/loading_engine.cpp
    src/context_sync.cpp
    include/offsets.hpp
    include/memory_bridge.hpp
    include/lua_vm_bridge.hpp
    include/loading_engine.hpp
    include/context_sync.hpp
)

add_executable(project_x_bridge
    src/main.cpp
)

target_link_libraries(project_x_bridge PRIVATE project_x_core)
