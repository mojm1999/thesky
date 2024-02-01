# 设置C++编译器
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")

# 限定CMake最低版本
cmake_minimum_required(VERSION 3.5)

# 定义项目名
project(TheSky)

# 设置警告信息输出级别为中等 -Weffc++
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# 要求必须使用 C++ 标准11版本 -std=c++11
# set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 定义头文件目录列表
set(PROJECT_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/thesky-src
    ${CMAKE_SOURCE_DIR}/thesky-net
    ${CMAKE_SOURCE_DIR}/thesky-evpp
    ${CMAKE_SOURCE_DIR}/3rdparty/inc
    # 添加更多目录
)

# 遍历列表添加头文件路径
foreach(DIR ${PROJECT_INCLUDE_DIRS})
    include_directories(${DIR})
endforeach()

# 添加链接库查找路径
link_directories(${CMAKE_SOURCE_DIR}/3rdparty/lib)

# 将子目录下的 .cc文件添加到变量中
file(GLOB SOURCES "thesky-src/*.cc")

# 编译可执行文件目标
add_executable(${PROJECT_NAME} ${SOURCES})

# 添加dll链接库（先生成再链接）
target_link_libraries(${PROJECT_NAME} event_core)
if(WIN32)
  target_link_libraries(${PROJECT_NAME} ws2_32)
endif()

# CMake命令
# cmake -G "MinGW Makefiles" ..
# cmake --build . *> build_output.html

# 删除当前目录下的全部文件
# rm -r -fo *