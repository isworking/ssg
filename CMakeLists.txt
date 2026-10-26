cmake_minimum_required(VERSION 3.16)
project(ssg VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(ssg
  src/main.cpp
  src/config.cpp
  src/document.cpp
  src/markdown.cpp
  src/ignore.cpp
  src/build.cpp
)

target_include_directories(ssg PRIVATE src)

if(MSVC)
  target_compile_options(ssg PRIVATE /W4)
else()
  target_compile_options(ssg PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME sites COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.sh $<TARGET_FILE:ssg>)

# CommonMark conformance. spec.json isn't vendored; fetch it with:
#   curl -o tests/spec.json https://spec.commonmark.org/0.31.2/spec.json
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/spec.json)
  add_test(NAME commonmark
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/commonmark.py
                   $<TARGET_FILE:ssg> ${CMAKE_CURRENT_SOURCE_DIR}/tests/spec.json --min 100)
endif()

include(GNUInstallDirs)
install(TARGETS ssg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/ssg.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES man/ssg.5 DESTINATION ${CMAKE_INSTALL_MANDIR}/man5)
