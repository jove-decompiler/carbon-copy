#include "collect.h"
#include "collect_impl.h"
#include <set>
#include <iostream>
#include <fstream>
#include <boost/graph/adj_list_serialize.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/icl/interval_map.hpp>
#define CARBON_BINARY
#ifdef CARBON_BINARY
#include <boost/archive/binary_oarchive.hpp>
#else
#include <boost/archive/text_oarchive.hpp>
#endif
#include <llvm/Support/raw_ostream.h>

using namespace std;
namespace fs = boost::filesystem;

namespace carbon {

static const bool debugMode = false;

static llvm::raw_ostream &
operator<<(llvm::raw_ostream &os,
           const boost::icl::discrete_interval<source_location_t> &intervl);

struct clang_source_file_hasher_t {
  size_t operator()(const clang_source_file_t &k) const {
    return hash_of_clang_source_file(k);
  }
};

struct source_file_hasher_t {
  size_t operator()(const source_file_t &k) const {
    return static_cast<size_t>(k);
  }
};

static boost::icl::discrete_interval<source_location_t>
interval_of_source_range(source_range_t src_rng) {
  return boost::icl::discrete_interval<source_location_t>::right_open(
      src_rng.beg, src_rng.end);
}

typedef set<depends_vertex_t> depends_vertex_set_t;

typedef boost::icl::interval_map<source_location_t, depends_vertex_set_t>
    source_ranges_to_vertex_map_t;

struct collector_priv {
  //
  // to-be-serialized
  //
  depends_t res;

  //
  // intermediaries
  //
  depends_context_t &depctx;

  // map between clang source files and source files
  unordered_map<clang_source_file_t, source_file_t, clang_source_file_hasher_t>
      src_f_map;

  // map between source files and clang source files
  unordered_map<source_file_t, clang_source_file_t, source_file_hasher_t>
      cl_src_f_map;

  // most important intermediary. maps from source ranges to vertices in the
  // depends graph so we can add edges upon uses
  vector<unique_ptr<source_ranges_to_vertex_map_t>> f_usr_src_rng_vert_map;
  vector<unique_ptr<source_ranges_to_vertex_map_t>> f_sys_src_rng_vert_map;

  unordered_map<clang_source_file_t, unsigned, clang_source_file_hasher_t>
      top_lvl_syst_src_idx_map;

  collector_priv() : depctx(res[boost::graph_bundle]) {}

  void clang_source_file(const clang_source_file_t &);

  // map clang source files to our own source files
  source_file_t map_clang_source_file(const clang_source_file_t &clrng);

  // map our own source files to clang source files
  clang_source_file_t map_source_file(const source_file_t &clrng);

  // map clang source ranges to our own source ranges
  source_range_t map_clang_source_range(const clang_source_range_t &);

  // map our own source ranges to clang source ranges
  clang_source_range_t map_source_range(const source_range_t &);

  source_ranges_to_vertex_map_t &
  source_range_vertex_map_of_source_file(const source_file_t &);

  string path_of_source_file(const source_file_t &);

  void code(const clang_source_range_t &cl_src_range);

  void use(const clang_source_range_t &user_cl_src_rng,
           const clang_source_range_t &usee_cl_src_rng);

  void use_if_user_exists(const clang_source_range_t &user,
                          const clang_source_range_t &usee);

  void follow_users_of(const clang_source_range_t &,
                       const clang_source_range_t &);

  void fixup_static_functions();
};

void collector_priv::clang_source_file(const clang_source_file_t &f) {
  auto it = src_f_map.find(f);
  if (it == src_f_map.end()) {
    // we need to:
    // 1. get the canonical file path to the source file from clang
    // 2. ask clang whether it is a system source file
    // 3. if so, add to system source files table, otherwise regular one
    // 4. take the index used in the previous step and save a mapping to it from
    // our given clang source file
    bool is_sys(clang_is_system_source_file(f));

    source_file_t _f;
    if (is_sys) {
      _f = syst_index_of_index(
          static_cast<unsigned>(depctx.syst_src_f_paths.size()));

      depctx.syst_src_f_paths.push_back(path_of_clang_source_file(f).string());

#if 0
      llvm::errs() << path_of_clang_source_file(f).string() << "  $$$\n";
#endif
      depctx.toplvl_syst_src_f_paths.push_back(
          path_of_clang_source_file(top_level_system_header(f)).string());
    } else {
      _f = static_cast<source_file_t>(depctx.user_src_f_paths.size());

      depctx.user_src_f_paths.push_back(path_of_clang_source_file(f).string());
    }

    src_f_map.insert({f, _f});
    cl_src_f_map.insert({_f, f});

    auto &f_src_rng_vert_map =
        is_sys ? f_sys_src_rng_vert_map : f_usr_src_rng_vert_map;
    f_src_rng_vert_map.emplace_back(new source_ranges_to_vertex_map_t);

    // create a vertex which denotes the entire file
    source_range_t entire_f_src_rng = {_f, location_entire_file_beg,
                                       location_entire_file_end};
    depends_vertex_set_t entire_f_v_container;
    depends_vertex_t entire_f_v = boost::add_vertex(res);
    res[entire_f_v] = entire_f_src_rng;
    entire_f_v_container.insert(entire_f_v);

    source_ranges_to_vertex_map_t &src_rng_to_vert_map =
        source_range_vertex_map_of_source_file(entire_f_src_rng.f);

    auto intervl = interval_of_source_range(entire_f_src_rng);

    src_rng_to_vert_map.add(make_pair(intervl, entire_f_v_container));
  }
}

source_file_t collector_priv::map_clang_source_file(const clang_source_file_t &f) {
  clang_source_file(f);
  auto it = src_f_map.find(f);
  return (*it).second;
}

clang_source_file_t collector_priv::map_source_file(const source_file_t &f) {
  auto it = cl_src_f_map.find(f);
  return (*it).second;
}

source_range_t
collector_priv::map_clang_source_range(const clang_source_range_t &clrng) {
  return {map_clang_source_file(clrng.f), clrng.beg, clrng.end};
}

clang_source_range_t
collector_priv::map_source_range(const source_range_t &srcrng) {
  return {map_source_file(srcrng.f), srcrng.beg, srcrng.end};
}

source_ranges_to_vertex_map_t &
collector_priv::source_range_vertex_map_of_source_file(const source_file_t &f) {
  unsigned idx = index_of_source_file(f);
  return is_system_source_file(f) ? *f_sys_src_rng_vert_map[idx].get()
                                  : *f_usr_src_rng_vert_map[idx].get();
}

string collector_priv::path_of_source_file(const source_file_t &f) {
  vector<string> &src_f_paths = is_system_source_file(f)
                                    ? depctx.syst_src_f_paths
                                    : depctx.user_src_f_paths;
  return src_f_paths[index_of_source_file(f)];
}

void collector_priv::use(const clang_source_range_t &user_cl_src_rng,
                         const clang_source_range_t &usee_cl_src_rng) {
  code(user_cl_src_rng);
  code(usee_cl_src_rng);

  source_range_t user_src_rng(map_clang_source_range(user_cl_src_rng));
  source_range_t usee_src_rng(map_clang_source_range(usee_cl_src_rng));

  source_ranges_to_vertex_map_t &user_src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(user_src_rng.f);
  source_ranges_to_vertex_map_t &usee_src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(usee_src_rng.f);

  auto user_vert_it = user_src_rng_to_vert_map.find(user_src_rng.beg);
  auto usee_vert_it = usee_src_rng_to_vert_map.find(usee_src_rng.beg);

  assert(user_vert_it != user_src_rng_to_vert_map.end() &&
         usee_vert_it != usee_src_rng_to_vert_map.end());

  const depends_vertex_set_t &user_vert_container = (*user_vert_it).second;
  const depends_vertex_set_t &usee_vert_container = (*usee_vert_it).second;

  assert(user_vert_container.size() == 1);
  assert(usee_vert_container.size() == 1);

  depends_vertex_t user_vert = *user_vert_container.begin();
  depends_vertex_t usee_vert = *usee_vert_container.begin();

  //
  // check for user using itself. when this occurs, do nothing.
  //
  if (user_vert == usee_vert)
    return;


  //
  // check for inverse edge already existing
  //
  if (boost::edge(usee_vert, user_vert, res).second) {
    // delete preexisting inverse edge
    boost::remove_edge(boost::edge(usee_vert, user_vert, res).first, res);
  }

  boost::add_edge(user_vert, usee_vert, res);
}

void collector_priv::use_if_user_exists(
    const clang_source_range_t &user_cl_src_rng,
    const clang_source_range_t &usee_cl_src_rng) {
  code(usee_cl_src_rng);

  source_range_t user_src_rng(map_clang_source_range(user_cl_src_rng));
  source_range_t usee_src_rng(map_clang_source_range(usee_cl_src_rng));

  source_ranges_to_vertex_map_t &user_src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(user_src_rng.f);
  source_ranges_to_vertex_map_t &usee_src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(usee_src_rng.f);

  auto user_vert_it = user_src_rng_to_vert_map.find(user_src_rng.beg);
  auto usee_vert_it = usee_src_rng_to_vert_map.find(usee_src_rng.beg);

  if (user_vert_it == user_src_rng_to_vert_map.end())
    return;

  assert(usee_vert_it != usee_src_rng_to_vert_map.end());

  const depends_vertex_set_t &user_vert_container = (*user_vert_it).second;
  const depends_vertex_set_t &usee_vert_container = (*usee_vert_it).second;

  assert(user_vert_container.size() == 1);
  assert(usee_vert_container.size() == 1);

  depends_vertex_t user_vert = *user_vert_container.begin();
  depends_vertex_t usee_vert = *usee_vert_container.begin();

  //
  // check for user using itself. when this occurs, do nothing.
  //
  if (user_vert == usee_vert)
    return;

  //
  // check for inverse edge already existing
  //
  if (boost::edge(usee_vert, user_vert, res).second)
    return;

  boost::add_edge(user_vert, usee_vert, res);
}

void collector_priv::follow_users_of(
    const clang_source_range_t &usee_cl_src_rng,
    const clang_source_range_t &user_cl_src_rng) {
  code(user_cl_src_rng);
  code(usee_cl_src_rng);

  source_range_t user_src_rng(map_clang_source_range(user_cl_src_rng));
  source_range_t usee_src_rng(map_clang_source_range(usee_cl_src_rng));

  source_ranges_to_vertex_map_t &user_src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(user_src_rng.f);
  source_ranges_to_vertex_map_t &usee_src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(usee_src_rng.f);

  auto user_vert_it = user_src_rng_to_vert_map.find(user_src_rng.beg);
  auto usee_vert_it = usee_src_rng_to_vert_map.find(usee_src_rng.beg);

  assert(user_vert_it != user_src_rng_to_vert_map.end() &&
         usee_vert_it != usee_src_rng_to_vert_map.end());

  const depends_vertex_set_t &user_vert_container = (*user_vert_it).second;
  const depends_vertex_set_t &usee_vert_container = (*usee_vert_it).second;

  assert(user_vert_container.size() == 1);
  assert(usee_vert_container.size() == 1);

  depends_vertex_t user_vert = *user_vert_container.begin();
  depends_vertex_t usee_vert = *usee_vert_container.begin();

  //
  // check for user using itself. when this occurs, do nothing.
  //
  if (user_vert == usee_vert)
    return;

  //
  // make the edges
  //
  depends_t::in_edge_iterator e_it, e_it_end;
  for (tie(e_it, e_it_end) = boost::in_edges(usee_vert, res); e_it != e_it_end;
       ++e_it) {
    depends_vertex_t to_v = boost::source(*e_it, res);

    if (boost::edge(to_v, user_vert, res).second)
      continue;

    res[boost::add_edge(user_vert, to_v, res).first].t = DEPENDS_FOLLOWS_EDGE;
  }
}

void collector_priv::code(const clang_source_range_t &cl_src_range) {
  source_range_t src_rng(map_clang_source_range(cl_src_range));
  auto intervl = interval_of_source_range(src_rng);

  source_ranges_to_vertex_map_t &src_rng_to_vert_map =
      source_range_vertex_map_of_source_file(src_rng.f);

  //
  // check for a pre-existing source ranges which overlap
  //
  auto preexist_it = src_rng_to_vert_map.find(intervl);
  if (preexist_it == src_rng_to_vert_map.end()) {
    // no overlapping source range

    depends_vertex_t v = boost::add_vertex(res);
    res[v] = src_rng;
    depends_vertex_set_t v_container;
    v_container.insert(v);

    src_rng_to_vert_map.add(make_pair(intervl, v_container));
    return;
  }

  //
  // if an existing mapping contains this one, then we have nothing to do
  //
  if (boost::icl::contains((*preexist_it).first, intervl)) {
    if (debugMode)
      llvm::errs() << path_of_source_file(src_rng.f) << ' '
                   << (*preexist_it).first << " ⊇ " << intervl << '\n';
    return;
  }

  // delete existing mapping(s), and insert new one
  list<pair<depends_vertex_t, DEPENDS_EDGE_TYPE>> in_verts;
  list<pair<depends_vertex_t, DEPENDS_EDGE_TYPE>> out_verts;

  //
  // we need to assemble a set of the vertices we'll be merging, because if
  // there are edges between them we must not add them.
  //
  unordered_set<depends_vertex_t> preexist_verts;

  do {
    if (!boost::icl::contains(intervl, (*preexist_it).first)) {
      if (debugMode) {
        ostringstream oss;
        oss << path_of_source_file(src_rng.f) << ' ' << intervl;
        string s(oss.str());
        string sp(s.size(), ' ');

        llvm::errs() << s << " ⊊\n" << sp << " ⊋ " << (*preexist_it).first
                     << '\n';
      }

      intervl = boost::icl::hull(intervl, (*preexist_it).first);
    } else {
      if (debugMode) {
        llvm::errs() << path_of_source_file(src_rng.f) << ' '
                     << (*preexist_it).first << " ⊆ " << intervl << '\n';
      }
    }

    assert((*preexist_it).second.size() == 1);
    auto preexist_v = *(*preexist_it).second.begin();
    preexist_verts.insert(preexist_v);

    {
      depends_t::in_edge_iterator e_it, e_it_end;
      for (tie(e_it, e_it_end) = boost::in_edges(preexist_v, res);
           e_it != e_it_end; ++e_it)
        in_verts.push_back(make_pair(boost::source(*e_it, res), res[*e_it].t));
    }

    {
      depends_t::out_edge_iterator e_it, e_it_end;
      for (tie(e_it, e_it_end) = boost::out_edges(preexist_v, res);
           e_it != e_it_end; ++e_it)
        out_verts.push_back(make_pair(boost::target(*e_it, res), res[*e_it].t));
    }

    //
    // remove the preexisting interval from the interval map
    //
    src_rng_to_vert_map.erase(*preexist_it);

    preexist_it = src_rng_to_vert_map.find(intervl);
  } while (preexist_it != src_rng_to_vert_map.end());

  //
  // filter-out the edges which go between the preexisting vertices
  //
  {
    auto it = in_verts.begin();
    while (it != in_verts.end()) {
      if (preexist_verts.find((*it).first) != preexist_verts.end())
        it = in_verts.erase(it);
      else
        ++it;
    }
  }

  {
    auto it = out_verts.begin();
    while (it != out_verts.end()) {
      if (preexist_verts.find((*it).first) != preexist_verts.end())
        it = out_verts.erase(it);
      else
        ++it;
    }
  }

  //
  // now it's safe to remove the preexisting vertices from the graph
  //
  for (auto preexist_v : preexist_verts) {
    boost::clear_vertex(preexist_v, res);
    boost::remove_vertex(preexist_v, res);
  }

  //
  // create new vertex in graph
  //
  depends_vertex_t v = boost::add_vertex(res);
  res[v] = src_rng;
  res[v].beg = intervl.lower();
  res[v].end = intervl.upper();

  //
  // add edges from old preexisting vertices to new vertex
  //
  for (auto in_v_pair : in_verts)
    res[boost::add_edge(in_v_pair.first, v, res).first].t = in_v_pair.second;
  for (auto out_v_pair : out_verts)
    res[boost::add_edge(v, out_v_pair.first, res).first].t = out_v_pair.second;

  //
  // store new vertex in interval map
  //
  depends_vertex_set_t v_container;
  v_container.insert(v);
  src_rng_to_vert_map.add(make_pair(intervl, v_container));

  if (debugMode)
    llvm::errs() << "  " << intervl << '\n';
}

static void vertex_interval_maps_of_graph(
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &syst_sl_vert_map,
    depends_t &g) {
  //
  // resize vectors
  //
  user_sl_vert_map.resize(g[boost::graph_bundle].user_src_f_paths.size());
  syst_sl_vert_map.resize(g[boost::graph_bundle].syst_src_f_paths.size());

  //
  // for every vertex, make an interval for it, add it to the map
  //
  depends_t::vertex_iterator vi, vi_end;
  for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi) {
    depends_vertex_t v = *vi;
    source_range_t &src_rng = g[v];

    if (src_rng.beg == location_entire_file_beg &&
        src_rng.end == location_entire_file_end)
      continue; // 'entire file' vertex

    if (src_rng.beg == location_dummy_beg && src_rng.end == location_dummy_end)
      continue; // dummy vertex

    unsigned f_idx = index_of_source_file(src_rng.f);

    boost::icl::interval_map<source_location_t, set<depends_vertex_t>>
        &sl_vert_map =
            is_system_source_file(src_rng.f) ? syst_sl_vert_map[f_idx]
                                             : user_sl_vert_map[f_idx];

    boost::icl::discrete_interval<source_location_t> intervl =
        boost::icl::discrete_interval<source_location_t>::right_open(
            src_rng.beg, src_rng.end);

    set<depends_vertex_t> v_container;
    v_container.insert(v);
    sl_vert_map.add(make_pair(intervl, v_container));
  }
}

void collector_priv::fixup_static_functions() {
  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      user_sl_vert_map;
  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      syst_sl_vert_map;
  vertex_interval_maps_of_graph(user_sl_vert_map, syst_sl_vert_map, res);

  //
  // for all code which depends on a static function declaration, search for a
  // static function definition and add a forward declaration edge to it.
  //
  for (auto &entry : res[boost::graph_bundle].static_decls) {
    // does a corresponding definition exist?
    auto defs_it = res[boost::graph_bundle].static_defs.find(entry.first);
    if (defs_it == res[boost::graph_bundle].static_defs.end()) {
      llvm::errs()
          << "warning: no definition found for static function declaration "
          << entry.first << '\n';
      continue;
    }

    auto def_it = (*defs_it).second.begin();
    auto def_sr = *def_it++;

    if (def_it != (*defs_it).second.end())
      llvm::errs() << "warning: multiple definitions found for static function "
                   << entry.first << '\n';

    // get definition vertex
    auto &def_sr_map = is_system_source_file(def_sr.f)
                           ? syst_sl_vert_map[index_of_source_file(def_sr.f)]
                           : user_sl_vert_map[index_of_source_file(def_sr.f)];
    auto def_vert_it = def_sr_map.find(def_sr.beg);
    if (def_vert_it == def_sr_map.end()) {
      llvm::errs()
          << "warning (bug): static function definition not found in source "
             "ranges map [symbol: "
          << entry.first << " offset: " << def_sr.beg << " file: "
          << (is_system_source_file(def_sr.f)
                  ? res[boost::graph_bundle]
                        .syst_src_f_paths[index_of_source_file(def_sr.f)]
                  : res[boost::graph_bundle]
                        .user_src_f_paths[index_of_source_file(def_sr.f)])
          << '\n';
      continue;
    }
    auto def_vert = *(*def_vert_it).second.begin();

    for (auto &dcl_sr : entry.second) {
      // get declaration vertex
      auto &dcl_sr_map = is_system_source_file(dcl_sr.f)
                             ? syst_sl_vert_map[index_of_source_file(dcl_sr.f)]
                             : user_sl_vert_map[index_of_source_file(dcl_sr.f)];
      auto dcl_vert_it = dcl_sr_map.find(dcl_sr.beg);
      if (dcl_vert_it == dcl_sr_map.end()) {
        llvm::errs()
            << "warning (bug): static function declaration not found in source "
               "ranges map [symbol: "
            << entry.first << " offset: " << dcl_sr.beg << " file: "
            << (is_system_source_file(dcl_sr.f)
                    ? res[boost::graph_bundle]
                          .syst_src_f_paths[index_of_source_file(dcl_sr.f)]
                    : res[boost::graph_bundle]
                          .user_src_f_paths[index_of_source_file(dcl_sr.f)])
            << '\n';
        continue;
      }
      auto dcl_vert = *(*dcl_vert_it).second.begin();

      res[boost::add_edge(dcl_vert, def_vert, res).first].t =
          DEPENDS_FWD_DECL_EDGE;
    }
  }
}

collector::collector() : priv(new collector_priv()) {}

collector::~collector() {}

void collector::set_args(const fs::path &_srcfp, const fs::path &_root_src_dir,
                         const fs::path &_root_bin_dir) {
  srcfp = _srcfp;
  root_src_dir = _root_src_dir;
  root_bin_dir = _root_bin_dir;
}

void collector::set_invocation_macros(
    const std::vector<std::pair<std::string, bool /*isUndef*/>> &Macros) {
  for (const std::pair<std::string, bool /* IsUndef */>& mac : Macros) {
    if (mac.second)
      priv->depctx.macros.und.insert(mac.first);
    else
      priv->depctx.macros.def.insert(mac.first);
  }
}

void collector::set_invocation_header_directories(
    const std::set<std::string> &dirs) {
  priv->depctx.include.dirs = dirs;
}

void collector::clang_source_file(const clang_source_file_t &f) {
  priv->clang_source_file(f);
}

void collector::code(const clang_source_range_t &cl_src_range) {
  priv->code(cl_src_range);
}

void collector::global_code(const clang_source_range_t &cl_src_range,
                            const std::string &sym, bool is_definition) {
  code(cl_src_range);

  source_range_t src_rng(priv->map_clang_source_range(cl_src_range));
  full_source_location_t full_src_loc = {src_rng.f, src_rng.beg};

  if (debugMode) {
    if (is_definition) {
      llvm::errs() << "GlobalDefinition '" << sym << "' "
           << (is_system_source_file(src_rng.f)
                   ? priv->depctx.syst_src_f_paths.at(
                         index_of_source_file(src_rng.f))
                   : priv->depctx.user_src_f_paths.at(
                         index_of_source_file(src_rng.f)))
           << ':' << is_system_source_file(src_rng.f) << ':' << src_rng.beg
           << ':' << src_rng.end << " (" << priv->depctx.syst_src_f_paths.size()
           << ", " << priv->depctx.user_src_f_paths.size() << ')' << '\n';
    } else {
      llvm::errs() << "GlobalDeclaration '" << sym << "' "
           << (is_system_source_file(src_rng.f)
                   ? priv->depctx.syst_src_f_paths.at(
                         index_of_source_file(src_rng.f))
                   : priv->depctx.user_src_f_paths.at(
                         index_of_source_file(src_rng.f)))
           << ':' << is_system_source_file(src_rng.f) << ':' << src_rng.beg
           << ':' << src_rng.end << " (" << priv->depctx.syst_src_f_paths.size()
           << ", " << priv->depctx.user_src_f_paths.size() << ')' << '\n';
    }
  }

  if (is_definition)
    priv->depctx.glbl_defs[sym] = full_src_loc;
  else
    priv->depctx.glbl_decls[sym].insert(full_src_loc);
}

void collector::static_code(const clang_source_range_t &cl_src_range,
                            const std::string &sym, bool is_definition) {
  code(cl_src_range);

  source_range_t src_rng(priv->map_clang_source_range(cl_src_range));
  full_source_location_t full_src_loc = {src_rng.f, src_rng.beg};

  if (debugMode) {
    if (is_definition) {
      llvm::errs() << "GlobalDefinition '" << sym << "' "
           << (is_system_source_file(src_rng.f)
                   ? priv->depctx.syst_src_f_paths.at(
                         index_of_source_file(src_rng.f))
                   : priv->depctx.user_src_f_paths.at(
                         index_of_source_file(src_rng.f)))
           << ':' << is_system_source_file(src_rng.f) << ':' << src_rng.beg
           << ':' << src_rng.end << " (" << priv->depctx.syst_src_f_paths.size()
           << ", " << priv->depctx.user_src_f_paths.size() << ')' << '\n';
    } else {
      llvm::errs() << "GlobalDeclaration '" << sym << "' "
           << (is_system_source_file(src_rng.f)
                   ? priv->depctx.syst_src_f_paths.at(
                         index_of_source_file(src_rng.f))
                   : priv->depctx.user_src_f_paths.at(
                         index_of_source_file(src_rng.f)))
           << ':' << is_system_source_file(src_rng.f) << ':' << src_rng.beg
           << ':' << src_rng.end << " (" << priv->depctx.syst_src_f_paths.size()
           << ", " << priv->depctx.user_src_f_paths.size() << ')' << '\n';
    }
  }

  if (is_definition)
    priv->depctx.static_defs[sym].insert(full_src_loc);
  else
    priv->depctx.static_decls[sym].insert(full_src_loc);
}


void collector::use(const clang_source_range_t &user_cl_src_rng,
                    const clang_source_range_t &usee_cl_src_rng) {
  priv->use(user_cl_src_rng, usee_cl_src_rng);
}

void collector::use_if_user_exists(
    const clang_source_range_t &user_cl_src_rng,
    const clang_source_range_t &usee_cl_src_rng) {
  priv->use_if_user_exists(user_cl_src_rng, usee_cl_src_rng);
}

void collector::follow_users_of(const clang_source_range_t &prior,
                                const clang_source_range_t &following) {
  priv->follow_users_of(prior, following);
}

void collector::write_carbon_output() {
  priv->fixup_static_functions();

  fs::path rel(fs::relative(srcfp, root_src_dir));
  if (rel.string().find("/..") != string::npos) {
    llvm::errs() << "collect : failed to compute relative path\n";
    return;
  }

  fs::path carbon_dir = root_bin_dir / ".carbon";
  fs::path carbon_src = carbon_dir / rel;

  fs::create_directories(carbon_src.parent_path());

  ofstream ofs(carbon_src.string() + ".carbon");
  {
#ifdef CARBON_BINARY
    boost::archive::binary_oarchive oa(ofs);
#else
    boost::archive::text_oarchive oa(ofs);
#endif
    oa << priv->res;
  }
}

llvm::raw_ostream &
operator<<(llvm::raw_ostream &os,
           const boost::icl::discrete_interval<source_location_t> &intervl) {
  ostringstream oss;
  oss << intervl;
  return os << oss.str();
}
}
