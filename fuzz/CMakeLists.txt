# Fuzz targets; see README.md. Included from the top-level CMakeLists.txt when SSG_FUZZ or
# SSG_FUZZ_STANDALONE is on.
set(SSG_FUZZ_TARGETS
  fuzz_markdown_html
  fuzz_markdown_text
  fuzz_sanitizer
  fuzz_template
  fuzz_toml
  fuzz_ignore
  fuzz_json
)

# With libFuzzer the core is built again with coverage instrumentation (-fsanitize=fuzzer-no-link).
# A separate copy keeps the ssg binary free of instrumentation that only libFuzzer's runtime
# provides symbols for.
if(SSG_FUZZ)
  list(TRANSFORM SSG_CORE_SOURCES PREPEND ${PROJECT_SOURCE_DIR}/ OUTPUT_VARIABLE _srcs)
  add_library(ssg_fuzz_core STATIC ${_srcs})
  target_include_directories(ssg_fuzz_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
  target_compile_options(ssg_fuzz_core PRIVATE -fsanitize=fuzzer-no-link)
  ssg_target_flags(ssg_fuzz_core)
  set(_core ssg_fuzz_core)
else()
  set(_core ssg_core)
endif()

foreach(t IN LISTS SSG_FUZZ_TARGETS)
  if(SSG_FUZZ)
    add_executable(${t} ${t}.cpp)
    target_compile_options(${t} PRIVATE -fsanitize=fuzzer)
    target_link_options(${t} PRIVATE -fsanitize=fuzzer)
  else()
    add_executable(${t} ${t}.cpp standalone_main.cpp)
  endif()
  target_link_libraries(${t} PRIVATE ${_core})
  ssg_target_flags(${t})
endforeach()
