cmake_minimum_required(VERSION 3.20.0)
project(CarbonCopy)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if (LLVM_VERSION_MAJOR LESS 19)
  message(FATAL_ERROR "This project isn't buldable with LLVM < 19.0.0.")
endif()


#
# compile flags
#
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -Wall -Wno-missing-template-arg-list-after-template-kw")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(collect)
add_subdirectory(extract)

# Find boost.
#
# By default, we build a bundled one and statically-link the library.
# If you want to link to the system's libboost_-*.so, pass
# -DCARBON_USE_SYSTEM_BOOST=ON.
option(CARBON_USE_SYSTEM_BOOST "Use system or vendored boost" OFF)
if(CARBON_USE_SYSTEM_BOOST)
  find_package(boost_system 1.89 REQUIRED)
  find_package(boost_filesystem 1.89 REQUIRED)
  find_package(boost_serialization 1.89 REQUIRED)
else()
  include(FetchContent)

  FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.gz
    URL_HASH SHA256=f51707c27359a0df0cac1beada86de31bb5eed5e8285592dadec384df99c2984
    USES_TERMINAL_DOWNLOAD TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP ON

    PATCH_COMMAND patch -p1 -R -i "${CMAKE_SOURCE_DIR}/patches/boost-graph.patch"
  )

  set(BOOST_INCLUDE_LIBRARIES system filesystem serialization graph interprocess icl program_options format)
  FetchContent_MakeAvailable(Boost)
endif()
