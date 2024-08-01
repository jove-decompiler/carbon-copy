#include "reachable.h"
#include <iostream>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/icl/interval_map.hpp>
#include <boost/graph/filtered_graph.hpp>

using namespace std;

namespace carbon {

struct reachable_ty_edges {
  const depends_t *g_ptr;

  reachable_ty_edges() : g_ptr(nullptr) {}
  reachable_ty_edges(const depends_t *g_ptr) : g_ptr(g_ptr) {}

  template <typename Edge> bool operator()(const Edge &e) const {
    const depends_t &g = *g_ptr;
    return g[e].t == DEPENDS_NORMAL_EDGE;
  }
};

struct reachable_edges {
  const depends_t *g_ptr;

  reachable_edges() : g_ptr(nullptr) {}
  reachable_edges(const depends_t *g_ptr) : g_ptr(g_ptr) {}

  template <typename Edge> bool operator()(const Edge &e) const {
    const depends_t &g = *g_ptr;
    return g[e].t != DEPENDS_FOLLOWS_EDGE;
  }
};

struct reachable_visitor : boost::default_bfs_visitor {
  unordered_set<depends_vertex_t> &reachable;

  reachable_visitor(unordered_set<depends_vertex_t> &reachable)
      : reachable(reachable) {}

  template <typename Graph>
  void discover_vertex(const depends_t::vertex_descriptor &s,
                       const Graph &) const {
    reachable.insert(s);
  }
};

static void vertex_interval_maps_of_graph(
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &user_out,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &syst_out,
    const depends_t &g);

set<code_t> reachable_code(unordered_set<code_t> &out, const depends_t &g,
                           const code_location_list_t &cll,
                           const global_symbol_list_t &gsl, bool only_tys) {
  set<code_t> res;

  cerr << "computing dependency subgraph" << endl;

  set<depends_vertex_t> verts;

  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      user_src_rng_vert_map;
  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      syst_src_rng_vert_map;

  vertex_interval_maps_of_graph(user_src_rng_vert_map, syst_src_rng_vert_map,
                                g);

  //
  // map code location list to vertices
  //

  // build inverse mapping from file paths to indices
  unordered_map<string, unsigned> user_f_idx_map;

  unsigned j = 0;
  for (const string &path : g[boost::graph_bundle].user_src_f_paths)
    user_f_idx_map[path] = j++;

#if 0
  for (const auto& entry : user_f_idx_map)
    cerr << entry.second << ' ' << entry.first << endl;

  j = 0;
  for (const auto& entry : g[boost::graph_bundle].user_src_f_paths)
    cerr << j++ << ' ' << entry << endl;
#endif

  for (const auto &cl : cll) {
    string path;
    unsigned off;
    tie(path, off) = cl;

    auto f_idx_it = user_f_idx_map.find(path);
    if (f_idx_it == user_f_idx_map.end()) {
      cerr << "source file '" << path
           << "' for given code location does not exist. files: " << endl;
      for (const string &_path : g[boost::graph_bundle].user_src_f_paths)
        cerr << "  " << _path << endl;
      assert(false);
    }

    auto v_it = user_src_rng_vert_map[(*f_idx_it).second].find(
        static_cast<source_location_t>(off));
    if (v_it == user_src_rng_vert_map[(*f_idx_it).second].end()) {
      cerr << "given code location does not exist in source file '" << path
           << '\'' << endl;
      exit(1);
    }

    auto v = *(*v_it).second.begin();

#if 0
    cerr << (*f_idx_it).second << " [" << g[v].beg << ", " << g[v].end
         << ")" << endl;
#endif

    res.insert(v);
    verts.insert(v);
  }

#if 0
  cerr << g[boost::graph_bundle].user_src_f_paths.size() << ' '
       << user_src_rng_vert_map.size() << endl;
#endif

  //
  // map global symbol list to vertices
  //
  for (const string &gs : gsl) {
    auto gl_it = g[boost::graph_bundle].glbl_defs.find(gs);
    if (gl_it != g[boost::graph_bundle].glbl_defs.end()) {
      const full_source_location_t &sl = (*gl_it).second;

      auto &def_sr_map =
          is_system_source_file(sl.f)
              ? syst_src_rng_vert_map[index_of_source_file(sl.f)]
              : user_src_rng_vert_map[index_of_source_file(sl.f)];
      auto def_vert_it = def_sr_map.find(sl.beg);
      if (def_vert_it == def_sr_map.end()) {
        cerr << "source range for symbol " << gs << " not found (skipping) "
             << endl;
        continue;
      }

      verts.insert(*(*def_vert_it).second.begin());
      continue;
    }

    auto st_it = g[boost::graph_bundle].static_defs.find(gs);
    if (st_it != g[boost::graph_bundle].static_defs.end() &&
        !(*st_it).second.empty()) {
      /* FIXME arbitrarily chosen static definition */
      const full_source_location_t &sl = *(*st_it).second.begin();

      auto &def_sr_map =
          is_system_source_file(sl.f)
              ? syst_src_rng_vert_map[index_of_source_file(sl.f)]
              : user_src_rng_vert_map[index_of_source_file(sl.f)];
      auto def_vert_it = def_sr_map.find(sl.beg);
      if (def_vert_it == def_sr_map.end()) {
        cerr << "source range for symbol " << gs << " not found (skipping) "
             << endl;
        continue;
      }

      verts.insert(*(*def_vert_it).second.begin());
      continue;
    }

    cerr << "symbol " << gs << " not found (skipping) " << endl;
  }

  if (verts.empty()) {
    cerr << "failed to extract code" << endl;
    exit(1);
  }

  //
  // search the graph from every vertex, recording which vertices are seen by
  // the search
  //
  for (auto v : verts) {
    reachable_visitor vis(out);

    map<depends_vertex_t, int> idx_map;
    map<depends_vertex_t, boost::default_color_type> clr_map;

    int i = 0;
    depends_t::vertex_iterator vi, vi_end;
    for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi)
      idx_map[*vi] = i++;

    if (only_tys) {
      reachable_ty_edges e_filter(&g);
      boost::filtered_graph<depends_t, reachable_ty_edges> fg(g, e_filter);

      boost::breadth_first_search(
          fg, v,
          boost::visitor(vis)
              .color_map(boost::associative_property_map<
                         map<depends_vertex_t, boost::default_color_type>>(
                  clr_map))
              .vertex_index_map(
                  boost::associative_property_map<map<depends_vertex_t, int>>(
                      idx_map)));
    } else {
      reachable_edges e_filter(&g);
      boost::filtered_graph<depends_t, reachable_edges> fg(g, e_filter);

      boost::breadth_first_search(
          fg, v,
          boost::visitor(vis)
              .color_map(boost::associative_property_map<
                         map<depends_vertex_t, boost::default_color_type>>(
                  clr_map))
              .vertex_index_map(
                  boost::associative_property_map<map<depends_vertex_t, int>>(
                      idx_map)));
    }
  }

  cerr << "computed dependency subgraph." << endl;

#if 0
  //
  // identify static definitions which are used that have the same name.
  // arbitrarily choose one of them, and move edges from the other static
  // definitions to the arbitrarily choosen one.
  //
  for (auto &entry : g[boost::graph_bundle].static_defs) {
    bool exists = false;

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
      auto def_vert = *(*def_vert_it).second.begin();

      auto it = out.find(def_vert);
      if (it != out.end()) {
        if (exists) {
          out.erase(it);
          cerr << "deleting duplicate definition of static function "
               << entry.first << endl;
        } else {
          exists = true;
        }
      }
    }
  }

  cerr << "removed duplicate static definitions." << endl;
#endif

  return res;
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
