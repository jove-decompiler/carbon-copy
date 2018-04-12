#pragma once
#include "collection.h"
#include <list>

namespace carbon {

void topologically_sort_code(std::list<code_t> &out, const depends_t &);
}
