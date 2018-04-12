#pragma once
#include "collection.h"
#include <list>
#include <unordered_set>
#include <set>

namespace carbon {

// file + offset
typedef std::pair<std::string, unsigned> code_location_t;

typedef std::list<code_location_t>    code_location_list_t;
typedef std::list<std::string>        global_symbol_list_t;

// returns set of code from given code locations
std::set<code_t> reachable_code(std::unordered_set<code_t> &out,
                                const depends_t &,
                                const code_location_list_t &,
                                const global_symbol_list_t &,
                                bool only_tys = false);
}
