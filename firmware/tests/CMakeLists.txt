cmake_minimum_required(VERSION 3.16)

project(stackchan_firmware_tests LANGUAGES C CXX)

enable_testing()

add_executable(motion_math_test
    motion_math_test.cpp
    ../main/stackchan/motion/motion_math.cpp
)

target_include_directories(motion_math_test PRIVATE
    ../main
)

target_compile_features(motion_math_test PRIVATE cxx_std_17)

add_test(NAME motion_math_test COMMAND $<TARGET_FILE:motion_math_test>)

set(CJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__cjson/cJSON" CACHE PATH "Directory containing cJSON.c and cJSON.h")
if(NOT EXISTS "${CJSON_DIR}/cJSON.c")
    message(FATAL_ERROR "Set CJSON_DIR to cJSON 1.7.19 source directory, or run idf.py reconfigure first")
endif()
add_executable(openai_web_search_test openai_web_search_test.cpp
    ../main/hal/openai_web_search.cpp "${CJSON_DIR}/cJSON.c")
target_include_directories(openai_web_search_test PRIVATE ../main/hal "${CJSON_DIR}")
target_compile_features(openai_web_search_test PRIVATE cxx_std_17)
add_test(NAME openai_web_search_test COMMAND $<TARGET_FILE:openai_web_search_test>)
