#include "collection.h"

using namespace std;

namespace carbon {

bool is_system_code(const depends_t &g, code_t v) {
  return is_system_source_file(g[v].f);
}

string system_header_of_code(const depends_t &g, code_t v) {
  assert(is_system_code(g, v));
  return g[boost::graph_bundle].syst_src_f_paths[index_of_source_file(g[v].f)];
}

string top_level_system_header_of_code(const depends_t &g, code_t v) {
  assert(is_system_code(g, v));
  return g[boost::graph_bundle]
      .toplvl_syst_src_f_paths[index_of_source_file(g[v].f)];
}

bool is_dummy_code(const depends_t &g, code_t v) {
  return g[v].beg == location_dummy_beg && g[v].end == location_dummy_end;
}

}
