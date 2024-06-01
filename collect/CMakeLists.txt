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

target_link_libraries(carbon-collect PRIVATE ${llvm_libs})

target_link_libraries(carbon-collect PRIVATE Boost::system)
target_link_libraries(carbon-collect PRIVATE Boost::format)
target_link_libraries(carbon-collect PRIVATE Boost::graph)
target_link_libraries(carbon-collect PRIVATE Boost::icl)
target_link_libraries(carbon-collect PRIVATE Boost::filesystem)
target_link_libraries(carbon-collect PRIVATE Boost::serialization)
target_link_libraries(carbon-collect PRIVATE Boost::program_options)

install(TARGETS carbon-collect RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}")
