#pragma once
#include "collection.h"
#include <vector>

namespace carbon {

struct code_reader_priv;
class code_reader {
  const depends_t &g;
  std::vector<unsigned> user_file_sizes;
  std::vector<unsigned> syst_file_sizes;

public:
  code_reader(const depends_t&);
  ~code_reader();

  std::string source_text(code_t);
  std::string source_description(code_t);
  std::string debug_source_description(code_t);
};

}
