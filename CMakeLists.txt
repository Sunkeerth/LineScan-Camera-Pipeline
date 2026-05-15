# ==============================================================================
# CMakeLists.txt
# Build configuration for CynLr Evaluation 1 (line-scan camera pipeline).
# Uses CMake 3.16 or higher, C++17, OpenCV for webcam support, and pthread for threading.
# ==============================================================================

# ------------------------------------------------------------------------------
# cmake_minimum_required(VERSION <min>...<max>)
# Sets the minimum required version of CMake for this project.
# If a lower version is used, CMake will error out.
# 3.16 is chosen because it provides good support for C++17 and modern features.
# ------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------------------------
# project(<project_name> LANGUAGES <lang1> <lang2> ...)
# Defines the project name and which programming languages are used.
# The name influences default output directories and build artifacts.
# LANGUAGES CXX tells CMake we only need a C++ compiler (no C or Fortran).
# ------------------------------------------------------------------------------
project(CynLr_Eval1 LANGUAGES CXX)

# ------------------------------------------------------------------------------
# set(<variable> <value> ...)
# Assigns a value to a CMake variable.
# CMAKE_CXX_STANDARD: the C++ standard to compile with (here 17).
# Setting this ensures the compiler uses -std=c++17 (or equivalent).
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)

# ------------------------------------------------------------------------------
# set(<variable> <value> ...) with property REQUIRED ON
# Makes the previous C++ standard setting mandatory.
# If the compiler does not support C++17, configuration will fail.
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# add_compile_options(<options>...)
# Adds compiler flags to the build for all targets (both executables).
# -Wall: enable most common warnings (e.g., unused variables, implicit conversions).
# -Wextra: enable extra warning flags (more thorough than -Wall).
# -O2: optimize for speed (level 2), typical for release builds.
# ------------------------------------------------------------------------------
add_compile_options(-Wall -Wextra -O2)

# ------------------------------------------------------------------------------
# find_package(<PackageName> REQUIRED COMPONENTS <components>)
# Locates and configures an external library (here OpenCV).
# REQUIRED: if OpenCV is not found, CMake stops with an error.
# COMPONENTS: list of OpenCV modules needed:
#   core   – basic image structures (Mat, etc.)
#   imgproc – image processing (cvtColor, resize, etc.)
#   videoio – video capture (VideoCapture, V4L2)
#   imgcodecs – image file I/O (imread, imwrite)
# ------------------------------------------------------------------------------
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs)

# ------------------------------------------------------------------------------
# message(<mode> <text>)
# Prints a message during CMake configuration.
# STATUS: displays a "normal" message (prefixed with --).
# Shows the detected OpenCV version for debugging / information.
# ------------------------------------------------------------------------------
message(STATUS "OpenCV version: ${OpenCV_VERSION}")

# ------------------------------------------------------------------------------
# include_directories(<dirs>...)
# Adds directories to the compiler's header search path (like -I).
# ${CMAKE_SOURCE_DIR} is the top directory containing the root CMakeLists.txt.
# The "include" folder holds all project headers (BoundedQueue.h, Pipeline.h, etc.).
# ${OpenCV_INCLUDE_DIRS} is set by find_package(OpenCV); contains OpenCV's headers.
# ------------------------------------------------------------------------------
include_directories(${CMAKE_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})

# ------------------------------------------------------------------------------
# set(<variable> <value>)
# Creates a list of source files that are common to both executables.
# PIPELINE_SOURCES holds the paths to the implementation files for the four blocks
# and the pipeline orchestrator.  These are used in both the main program and tests.
# ------------------------------------------------------------------------------
set(PIPELINE_SOURCES
    src/DataGenerationBlock.cpp
    src/FilterThresholdBlock.cpp
    src/Pipeline.cpp
    src/LabellingBlock.cpp
    src/TracingBlock.cpp
)

# ------------------------------------------------------------------------------
# add_executable(<name> <sources>...)
# Defines an executable target.
# CynLr_Eval1: the production executable (runs the pipeline with webcam/RNG/CSV).
# Sources: common pipeline sources + the main entry point src/main.cpp.
# ------------------------------------------------------------------------------
add_executable(CynLr_Eval1  ${PIPELINE_SOURCES} src/main.cpp)

# ------------------------------------------------------------------------------
# target_link_libraries(<target> <PRIVATE|PUBLIC|INTERFACE> <libraries>...)
# Specifies libraries to link against the target.
# PRIVATE: libraries are needed only for linking this target, not for dependents.
# ${OpenCV_LIBS}: expanded to the list of OpenCV libraries (e.g., opencv_core, opencv_imgproc).
# pthread: POSIX threads library (required for std::thread and mutex).
# ------------------------------------------------------------------------------
target_link_libraries(CynLr_Eval1  PRIVATE ${OpenCV_LIBS} pthread)

# ------------------------------------------------------------------------------
# Second executable: run_tests
# Used for running unit tests (the tests/tests.cpp file).
# Shares the same pipeline source files (so that tests can instantiate blocks).
# ------------------------------------------------------------------------------
add_executable(run_tests    ${PIPELINE_SOURCES} tests/tests.cpp)

# ------------------------------------------------------------------------------
# Same linking configuration for the test executable.
# PRIVATE ensures that test code does not propagate OpenCV/pthread requirements
# to its dependents (none exist).
# ------------------------------------------------------------------------------
target_link_libraries(run_tests    PRIVATE ${OpenCV_LIBS} pthread)