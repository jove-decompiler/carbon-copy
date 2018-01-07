#pragma once
#include "collection.h"
#include <unordered_map>

namespace carbon {

void build_static_function_definitions_map(const collection_t &, std::unordered_map<code_t, bool*> &out);

}

