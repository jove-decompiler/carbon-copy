#include "collection.h"
#include "collection_impl.h"
#include <iostream>

using namespace std;

namespace carbon {

bool is_system_code(const collection_t &g, code_t v) {
  return is_system_source_file(g.g[v].f);
}

string system_header_of_code(const collection_t &g, code_t v) {
  assert(is_system_code(g, v));
  return g.g[boost::graph_bundle]
      .syst_src_f_paths[index_of_source_file(g.g[v].f)];
}

string top_level_system_header_of_code(const collection_t &g, code_t v) {
  assert(is_system_code(g, v));
  return g.g[boost::graph_bundle]
      .toplvl_syst_src_f_paths[index_of_source_file(g.g[v].f)];
}

bool is_dummy_code(const collection_t & g, code_t v) {
  return g.g[v].beg == location_dummy_beg && g.g[v].end == location_dummy_end;
}

}
