#include "static.h"
#include <vector>
#include <boost/icl/interval_map.hpp>
#include <iostream>

using namespace std;

namespace carbon {

static void vertex_interval_maps_of_graph(
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &user_out,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &syst_out,
    const depends_t &g);

void build_static_function_definitions_map(
    const depends_t &g, std::unordered_map<code_t, bool *> &out) {
  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      user_src_rng_vert_map;
  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      syst_src_rng_vert_map;

  vertex_interval_maps_of_graph(user_src_rng_vert_map, syst_src_rng_vert_map,
                                g);

  for (auto &entry : g[boost::graph_bundle].static_defs) {
    bool *b = new bool(false);

    for (auto &def_sr : entry.second) {
      // get definition vertex
      auto &def_sr_map =
          is_system_source_file(def_sr.f)
              ? syst_src_rng_vert_map[index_of_source_file(def_sr.f)]
              : user_src_rng_vert_map[index_of_source_file(def_sr.f)];
      auto def_vert_it = def_sr_map.find(def_sr.beg);
      if (def_vert_it == def_sr_map.end()) {
        cerr << "warning (bug): static function definition not found in source "
                "ranges map [symbol: "
             << entry.first << " offset: " << def_sr.beg << " file: "
             << (is_system_source_file(def_sr.f)
                     ? g[boost::graph_bundle]
                           .syst_src_f_paths[index_of_source_file(def_sr.f)]
                     : g[boost::graph_bundle]
                           .user_src_f_paths[index_of_source_file(def_sr.f)])
             << endl;
        continue;
      }

      out[*(*def_vert_it).second.begin()] = b;

#if 0
      cerr << "  [offset: " << def_sr.beg << " file: "
           << (is_system_source_file(def_sr.f)
                   ? g[boost::graph_bundle]
                         .syst_src_f_paths[index_of_source_file(def_sr.f)]
                   : g[boost::graph_bundle]
                         .user_src_f_paths[index_of_source_file(def_sr.f)])
           << ']' << endl;
#endif
    }
  }
}

static void vertex_interval_maps_of_graph(
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &user_out,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &syst_out,
    const depends_t &g) {
  //
  // resize vectors
  //
  user_out.resize(g[boost::graph_bundle].user_src_f_paths.size());
  syst_out.resize(g[boost::graph_bundle].syst_src_f_paths.size());

  //
  // for every vertex, make an interval for it, add it to the map
  //
  depends_t::vertex_iterator vi, vi_end;
  for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi) {
    depends_vertex_t v = *vi;
    const source_range_t &src_rng = g[v];

    if (src_rng.beg == location_entire_file_beg &&
        src_rng.end == location_entire_file_end)
      continue; // 'entire file' vertex

    if (src_rng.beg == location_dummy_beg &&
        src_rng.end == location_dummy_end)
      continue; // dummy vertex

    unsigned f_idx = index_of_source_file(src_rng.f);

    boost::icl::interval_map<source_location_t,
                             set<depends_vertex_t>> &src_rng_to_vert_map =
        is_system_source_file(src_rng.f) ? syst_out[f_idx] : user_out[f_idx];

    boost::icl::discrete_interval<source_location_t> intervl =
        boost::icl::discrete_interval<source_location_t>::right_open(
            src_rng.beg, src_rng.end);

    set<depends_vertex_t> v_container;
    v_container.insert(v);
    src_rng_to_vert_map.add(make_pair(intervl, v_container));
  }
}
}
