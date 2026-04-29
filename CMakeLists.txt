cmake_minimum_required(VERSION 3.16)
project(CynLr_Eval1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -O2)

# OpenCV — install with: sudo apt install libopencv-dev
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")

include_directories(${CMAKE_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})

set(PIPELINE_SOURCES
    src/DataGenerationBlock.cpp
    src/FilterThresholdBlock.cpp
    src/Pipeline.cpp
)

add_executable(CynLr_Eval1  ${PIPELINE_SOURCES} src/main.cpp)
target_link_libraries(CynLr_Eval1  PRIVATE ${OpenCV_LIBS} pthread)

add_executable(run_tests    ${PIPELINE_SOURCES} tests/tests.cpp)
target_link_libraries(run_tests    PRIVATE ${OpenCV_LIBS} pthread)
