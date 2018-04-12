#pragma once
#include <string>
#include <collect_impl.h>

namespace carbon {

typedef depends_vertex_t code_t;

bool is_system_code(const depends_t &, code_t);
std::string top_level_system_header_of_code(const depends_t &, code_t);
std::string system_header_of_code(const depends_t &, code_t);
bool is_dummy_code(const depends_t &, code_t);

}
