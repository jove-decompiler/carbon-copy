add_executable(carbon-extract
  src/carbon_extract.cpp
  src/link.cpp
  src/toposort.cpp
  src/reachable.cpp
  src/code_reader.cpp
  src/read_collection.cpp
  src/graphviz.cpp
  src/collection.cpp
  src/static.cpp
)

target_include_directories(carbon-extract PRIVATE
  include
  ../collect/include
)

target_link_libraries(carbon-extract PRIVATE Boost::system)
target_link_libraries(carbon-extract PRIVATE Boost::format)
target_link_libraries(carbon-extract PRIVATE Boost::graph)
target_link_libraries(carbon-extract PRIVATE Boost::icl)
target_link_libraries(carbon-extract PRIVATE Boost::filesystem)
target_link_libraries(carbon-extract PRIVATE Boost::serialization)
target_link_libraries(carbon-extract PRIVATE Boost::program_options)

install(TARGETS carbon-extract RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
