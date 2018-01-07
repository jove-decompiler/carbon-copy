#pragma once
#include "collection.h"
#include <memory>

namespace carbon {

struct code_reader_priv;
class code_reader {
  std::unique_ptr<code_reader_priv> priv;

public:
  code_reader(const collection_t&);
  ~code_reader();

  std::string source_text(code_t);
  std::string source_description(code_t);
  std::string debug_source_description(code_t);
};

}
