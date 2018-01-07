#pragma once
#include <string>

namespace carbon {

struct collection_t;

typedef void* code_t;

bool is_system_code(const collection_t &, code_t);
std::string top_level_system_header_of_code(const collection_t &, code_t);
std::string system_header_of_code(const collection_t &, code_t);
bool is_dummy_code(const collection_t &, code_t);

}
