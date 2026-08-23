cmake_minimum_required(VERSION 3.16)
project(ssg LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ssg
  src/main.cpp
  src/config.cpp
  src/document.cpp
  src/markdown.cpp
  src/build.cpp
)

target_include_directories(ssg PRIVATE src)

if(MSVC)
  target_compile_options(ssg PRIVATE /W4)
else()
  target_compile_options(ssg PRIVATE -Wall -Wextra -Wpedantic)
endif()
