include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_library(carbon-collect SHARED
  src/carbon_collect.cpp
  src/collect.cpp
  src/utilities_clang.cpp
)

target_include_directories(carbon-collect PRIVATE
  include
)

llvm_map_components_to_libnames(llvm_libs support core irreader)

target_link_libraries(carbon-collect
  ${llvm_libs}

  boost_serialization
  boost_filesystem
  boost_system
)
