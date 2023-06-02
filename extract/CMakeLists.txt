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

target_link_libraries(carbon-extract PRIVATE
  boost_filesystem
  boost_serialization
  boost_program_options
)
