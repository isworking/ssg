cmake_minimum_required(VERSION 3.16)
project(ssg VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The version lives here only. Release builds set the suffix, e.g. -DSSG_VERSION_SUFFIX=-alpha.1,
# or "" for a final release.
set(SSG_VERSION_SUFFIX "-dev" CACHE STRING "Appended to the project version, e.g. -alpha.1")
set(SSG_VERSION "${PROJECT_VERSION}${SSG_VERSION_SUFFIX}")

# The commit is captured when CMake configures, not on every build: rerun cmake to refresh it.
# "unknown" outside a git checkout (e.g. a source tarball) or without git. Only ssg's own
# checkout counts: git would otherwise find a repository above a tarball unpacked inside one.
# (.git is a file in worktrees and submodules.) --exclude=* keeps tags out, so this is always
# a hash, never v1.0.0 or v1.0.0-1-gabc1234.
set(SSG_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --exclude=*
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE _ssg_commit OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE _ssg_git_rc ERROR_QUIET)
  if(_ssg_git_rc EQUAL 0 AND NOT _ssg_commit STREQUAL "")
    set(SSG_COMMIT "${_ssg_commit}")
  endif()
endif()

option(SSG_WERROR "Treat compiler warnings as errors" OFF)
option(SSG_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
include(GNUInstallDirs)
option(SSG_FUZZ "Build the libFuzzer targets in fuzz/ (needs LLVM clang)" OFF)
option(SSG_FUZZ_STANDALONE "Build the fuzz targets as plain programs that replay files" OFF)

if(SSG_FUZZ)
  if(SSG_FUZZ_STANDALONE)
    message(FATAL_ERROR "SSG_FUZZ and SSG_FUZZ_STANDALONE are mutually exclusive")
  endif()
  # AppleClang matches "Clang" too but ships without libFuzzer.
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "SSG_FUZZ needs LLVM clang with libFuzzer (CMAKE_CXX_COMPILER_ID is "
                        "${CMAKE_CXX_COMPILER_ID}); e.g. -DCMAKE_CXX_COMPILER=clang++. "
                        "Without one, use -DSSG_FUZZ_STANDALONE=ON to replay inputs. See fuzz/README.md.")
  endif()
endif()

# Warning, -Werror and sanitizer flags for every target (core, ssg, fuzzers).
function(ssg_target_flags target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(SSG_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(SSG_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  if(SSG_SANITIZE)
    if(MSVC)
      message(FATAL_ERROR "SSG_SANITIZE needs GCC or Clang")
    endif()
    target_compile_options(${target} PRIVATE
                           -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# Everything but main(), so the fuzz targets can link it too.
set(SSG_CORE_SOURCES
  src/config.cpp
  src/toml.cpp
  src/css.cpp
  src/document.cpp
  src/markdown.cpp
  src/ignore.cpp
  src/features.cpp
  src/deploy.cpp
  src/json.cpp
  src/plugin.cpp
  src/build.cpp
)
add_library(ssg_core STATIC ${SSG_CORE_SOURCES})
target_include_directories(ssg_core PUBLIC src)
ssg_target_flags(ssg_core)

add_executable(ssg src/main.cpp src/serve.cpp src/scaffold.cpp src/completions.cpp)
target_link_libraries(ssg PRIVATE ssg_core)
target_compile_definitions(ssg PRIVATE SSG_VERSION="${SSG_VERSION}" SSG_COMMIT="${SSG_COMMIT}")
ssg_target_flags(ssg)

if(SSG_FUZZ OR SSG_FUZZ_STANDALONE)
  add_subdirectory(fuzz)
endif()

enable_testing()
add_test(NAME sites COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.sh $<TARGET_FILE:ssg>)
add_test(NAME docs COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/docs_check.sh)

# C++ checks on internals the sites tests can't see, e.g. the stored [plugin.<name>] tables.
add_executable(config_test tests/unit/config_test.cpp)
target_link_libraries(config_test PRIVATE ssg_core)
ssg_target_flags(config_test)
add_test(NAME unit COMMAND config_test ${CMAKE_CURRENT_BINARY_DIR}/config_test.tmp)

# The first-party plugins (ssg-plugin-highlight, -images, -search) are optional targets.
option(SSG_PLUGINS "Build and install the first-party plugins in plugins/" ON)
if(SSG_PLUGINS)
  foreach(plugin highlight images search)
    add_executable(ssg-plugin-${plugin} plugins/${plugin}.cpp)
    target_link_libraries(ssg-plugin-${plugin} PRIVATE ssg_core)
    ssg_target_flags(ssg-plugin-${plugin})
    install(TARGETS ssg-plugin-${plugin} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endforeach()
endif()

# A protocol 1 plugin for the plugin tests (tests/sites/plugin*.check find it next to ssg).
add_executable(ssg-plugin-mock tests/plugin/mock_plugin.cpp)
target_link_libraries(ssg-plugin-mock PRIVATE ssg_core)
ssg_target_flags(ssg-plugin-mock)

add_executable(markdown_doc_test tests/unit/markdown_doc_test.cpp)
target_link_libraries(markdown_doc_test PRIVATE ssg_core)
ssg_target_flags(markdown_doc_test)
add_test(NAME markdown_doc COMMAND markdown_doc_test)

add_executable(json_test tests/unit/json_test.cpp)
target_link_libraries(json_test PRIVATE ssg_core)
ssg_target_flags(json_test)
add_test(NAME json COMMAND json_test)

add_executable(css_test tests/unit/css_test.cpp)
target_link_libraries(css_test PRIVATE ssg_core)
ssg_target_flags(css_test)
add_test(NAME css COMMAND css_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/css)

# CommonMark conformance. spec.json isn't vendored; fetch it with:
#   curl -o tests/spec.json https://spec.commonmark.org/0.31.2/spec.json
# (CI does, and checks its sha256.) --min is a pass rate in percent: 100 means all 652 examples,
# so losing any one fails the test.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/spec.json)
  add_test(NAME commonmark
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/commonmark.py
                   $<TARGET_FILE:ssg> ${CMAKE_CURRENT_SOURCE_DIR}/tests/spec.json --min 100)
endif()

install(TARGETS ssg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/ssg.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES man/ssg.5 DESTINATION ${CMAKE_INSTALL_MANDIR}/man5)
