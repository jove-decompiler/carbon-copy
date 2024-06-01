cmake_minimum_required(VERSION 3.20.0)
project(CarbonCopy)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

#
# compile flags
#
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

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
  find_package(boost_system 1.85 REQUIRED)
  find_package(boost_filesystem 1.85 REQUIRED)
  find_package(boost_serialization 1.85 REQUIRED)
else()
  include(FetchContent)

  FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.xz
    URL_MD5 893b5203b862eb9bbd08553e24ff146a
    DOWNLOAD_EXTRACT_TIMESTAMP ON
  )

  set(BOOST_INCLUDE_LIBRARIES system filesystem serialization graph icl program_options format)
  FetchContent_MakeAvailable(Boost)
endif()
