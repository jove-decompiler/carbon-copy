#pragma once
#include "code_reader.h"
#include <fstream>
#include <unordered_set>

namespace carbon {

void output_graphviz_of_collection(std::ostream &out, code_reader &cr,
                                   const depends_t &);
void output_graphviz_of_reachable_code(std::ostream &out, code_reader &cr,
                                       const std::unordered_set<code_t> &,
                                       const depends_t &);
void output_graphviz_of_user_code(std::ostream &out, code_reader &cr,
                                  const depends_t &);
void output_graphviz_of_reachable_user_code(
    std::ostream &out, code_reader &cr, const std::unordered_set<code_t> &code,
    const depends_t &);
}
