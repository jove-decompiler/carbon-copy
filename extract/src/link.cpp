#include "link.h"
#include "collection_impl.h"
#include "read_collection.h"
#include <boost/icl/interval_map.hpp>
#include <collect_impl.h>
#include <iostream>
#include <queue>
#include <set>

using namespace std;
namespace fs = boost::filesystem;

namespace carbon {

static void link_into(
    depends_t &into, depends_t &from,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_syst_sl_vert_map);
static void relocate(
    depends_t &into, depends_t &from,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_syst_sl_vert_map);
static void resolve_references(depends_t &,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_syst_sl_vert_map);
static void vertex_interval_maps_of_graph(
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &syst_sl_vert_map,
    depends_t &g);

void link(collection_t &res, const collection_sources_t &cfl) {
  depends_t &into = res.g;

  cerr << "linking dependency graphs..." << endl;

  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      into_user_sl_vert_map;
  vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
      into_syst_sl_vert_map;
  vertex_interval_maps_of_graph(into_user_sl_vert_map, into_syst_sl_vert_map,
                                into);
  for (const fs::path &fp : cfl.second) {
    collection_t tmp;
    depends_t &g = tmp.g;

    cerr << "linking "
         << fs::relative(fp, cfl.first).replace_extension("").string()
         << endl;

    read_collection_file(tmp, fp);

    for (const string& sym : g[boost::graph_bundle].macros.def)
      into[boost::graph_bundle].macros.def.insert(sym);

    for (const string& sym : g[boost::graph_bundle].macros.und)
      into[boost::graph_bundle].macros.und.insert(sym);

    for (const string& dir : g[boost::graph_bundle].include.dirs)
      into[boost::graph_bundle].include.dirs.insert(dir);

#if 0
      cerr << "linking " << fp.stem().filename() << endl;
#else
#endif

#if 0
    cerr << "---------------------------------------------------------------"
         << endl;
    cerr << fp << endl;
    depends_t::vertex_iterator vi, vi_end;
    for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi) {
      depends_vertex_t v = *vi;
      source_range_t &src_rng = g[v];
      cerr << " [" << src_rng.beg << ", " << src_rng.end << ")" << endl;
    }
#endif

#if 0
    cerr << "---------------------------------------------------------------"
         << endl << "lhs graph user paths:" << endl;
    for (unsigned i = 0; i < into[boost::graph_bundle].user_src_f_paths.size(); ++i)
      cerr << "  " << into[boost::graph_bundle].user_src_f_paths[i] << endl;
    cerr << "----------------------------" << endl << "lhs graph system paths:" << endl;
    for (unsigned i = 0; i < into[boost::graph_bundle].syst_src_f_paths.size(); ++i)
      cerr << "  " << into[boost::graph_bundle].syst_src_f_paths[i] << endl;

    cerr << "----------------------------" << endl << "rhs graph user paths:" << endl;
    for (unsigned i = 0; i < g[boost::graph_bundle].user_src_f_paths.size(); ++i)
      cerr << g[boost::graph_bundle].user_src_f_paths[i] << endl;
    cerr << "----------------------------" << endl << "rhs graph system paths:" << endl;
    for (unsigned i = 0; i < g[boost::graph_bundle].syst_src_f_paths.size(); ++i)
      cerr << "  " << g[boost::graph_bundle].syst_src_f_paths[i] << endl;
#endif

#if 0
    cerr << "relocate" << endl;
#endif
    relocate(into, g, into_user_sl_vert_map, into_syst_sl_vert_map);

#if 0
    cerr << "link_into" << endl;
#endif
    link_into(into, g, into_user_sl_vert_map, into_syst_sl_vert_map);
  }

  resolve_references(into, into_user_sl_vert_map, into_syst_sl_vert_map);

  cerr << "finished linking dependency graphs (" << boost::num_vertices(into)
       << " vertices, " << boost::num_edges(into) << " edges)." << endl;
}

void relocate(
    depends_t &into, depends_t &from,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_syst_sl_vert_map) {
  depends_context_t &into_depctx = into[boost::graph_bundle];
  depends_context_t &from_depctx = from[boost::graph_bundle];

  //
  // prepare to merge
  //
  into_depctx.user_src_f_paths.reserve(from_depctx.user_src_f_paths.size() +
                                       into_depctx.user_src_f_paths.size());
  into_depctx.syst_src_f_paths.reserve(from_depctx.user_src_f_paths.size() +
                                       into_depctx.user_src_f_paths.size());
  into_depctx.toplvl_syst_src_f_paths.reserve(
      into_depctx.toplvl_syst_src_f_paths.size() +
      from_depctx.toplvl_syst_src_f_paths.size());

  //
  // add the given graph's files to the output graph and adjust the file indices
  // of given graph to fit nicely within the destination graph's file tables
  //

  // build mappings between destination graph's file paths to indices
  unordered_map<string, source_file_t> user_f_map;
  unordered_map<string, source_file_t> syst_f_map;

  for (unsigned i = 0; i < into_depctx.user_src_f_paths.size(); ++i)
    user_f_map[into_depctx.user_src_f_paths[i]] = static_cast<source_file_t>(i);

  for (unsigned i = 0; i < into_depctx.syst_src_f_paths.size(); ++i)
    syst_f_map[into_depctx.syst_src_f_paths[i]] = syst_index_of_index(i);

// build set of user and system file paths that destination graph knows about
#if 0
  unordered_map<string, unsigned> into_user_src_idx_paths;
  unordered_map<string, unsigned> into_syst_src_idx_paths;

  for (unsigned i = 0; i < into_depctx.user_src_f_paths.size(); ++i)
    into_user_src_idx_paths[into_depctx.user_src_f_paths[i]] = i;

  for (unsigned i = 0; i < into_depctx.syst_src_f_paths.size(); ++i)
    into_syst_src_idx_paths[into_depctx.syst_src_f_paths[i]] = i;
#endif

  // building mappings between given graph's files to destination graph's files
  unordered_map<source_file_t, source_file_t> f_map;

  for (unsigned i = 0; i < from_depctx.user_src_f_paths.size(); ++i) {
    auto it = user_f_map.find(from_depctx.user_src_f_paths[i]);
    if (it != user_f_map.end()) {
      f_map[static_cast<source_file_t>(i)] =
          static_cast<source_file_t>((*it).second);
    } else {
      f_map[static_cast<source_file_t>(i)] =
          static_cast<source_file_t>(into_depctx.user_src_f_paths.size());

      user_f_map[from_depctx.user_src_f_paths[i]] =
          static_cast<source_file_t>(into_depctx.user_src_f_paths.size());

      into_depctx.user_src_f_paths.push_back(from_depctx.user_src_f_paths[i]);
    }
  }

  for (unsigned i = 0; i < from_depctx.syst_src_f_paths.size(); ++i) {
    auto it = syst_f_map.find(from_depctx.syst_src_f_paths[i]);
    if (it != syst_f_map.end()) {
      f_map[syst_index_of_index(i)] = (*it).second;
    } else {
      f_map[syst_index_of_index(i)] =
          static_cast<source_file_t>(syst_index_of_index(
              static_cast<unsigned>(into_depctx.syst_src_f_paths.size())));

      syst_f_map[from_depctx.syst_src_f_paths[i]] = syst_index_of_index(
          static_cast<unsigned>(into_depctx.syst_src_f_paths.size()));

      into_depctx.syst_src_f_paths.push_back(from_depctx.syst_src_f_paths[i]);
      into_depctx.toplvl_syst_src_f_paths.push_back(from_depctx.toplvl_syst_src_f_paths[i]);
    }
  }

  //
  // adjust file indices for global and static functions
  //
  auto map_full_source_location_set_files = [&](
      const set<full_source_location_t> &in) -> set<full_source_location_t> {
    set<full_source_location_t> res;
    transform(in.begin(), in.end(), inserter(res, res.begin()),
              [&](full_source_location_t fsl) -> full_source_location_t {
                fsl.f = f_map[fsl.f];
                return fsl;
              });
    return res;
  };

  //
  // adjust global function file indices
  //
  for (auto &entry : from_depctx.glbl_defs)
    entry.second.f = f_map[entry.second.f];

  for (auto &entry : from_depctx.glbl_decls)
    entry.second = map_full_source_location_set_files(entry.second);

  //
  // adjust static function file indices
  //
  for (auto &entry : from_depctx.static_defs)
    entry.second = map_full_source_location_set_files(entry.second);

  for (auto &entry : from_depctx.static_decls)
    entry.second = map_full_source_location_set_files(entry.second);

  // adjust vertex source location file indices
  depends_t::vertex_iterator vi, vi_end;
  for (tie(vi, vi_end) = vertices(from); vi != vi_end; ++vi)
    from[*vi].f = f_map[from[*vi].f];

  //
  // resize vectors
  //
  into_user_sl_vert_map.resize(into[boost::graph_bundle].user_src_f_paths.size());
  into_syst_sl_vert_map.resize(into[boost::graph_bundle].syst_src_f_paths.size());
}

// precondition: from is relocated to be within into's file index namespace
void link_into(
    depends_t &into, depends_t &from,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &into_syst_sl_vert_map) {
  //
  // add the vertices of given graph to destination, unless corresponding
  // vertices in the destination graph already exist with the same source range
  //
  depends_t::vertex_iterator vi, vi_end;
  for (tie(vi, vi_end) = boost::vertices(from); vi != vi_end; ++vi) {
    source_range_t &src_rng = from[*vi];

#if 0
    cerr << f_idx << ':' << is_system_source_file(src_rng.f) << " (" << into_syst_sl_vert_map.size() << ", " << into_user_sl_vert_map.size() << ')' << endl;
#endif

    boost::icl::interval_map<source_location_t, set<depends_vertex_t>>
        &sl_vert_map =
            is_system_source_file(src_rng.f)
                ? into_syst_sl_vert_map.at(index_of_source_file(src_rng.f))
                : into_user_sl_vert_map.at(index_of_source_file(src_rng.f));

    boost::icl::discrete_interval<source_location_t> intervl =
        boost::icl::discrete_interval<source_location_t>::right_open(
            src_rng.beg, src_rng.end);

    // consider partial intersection?
    if (sl_vert_map.find(intervl) == sl_vert_map.end()) {
      depends_vertex_t v = boost::add_vertex(into);
      into[v] = src_rng;

      set<depends_vertex_t> v_container;
      v_container.insert(v);
      sl_vert_map.add(make_pair(intervl, v_container));
    }
  }

  //
  // add the edges of given graph to destination, unless corresponding edges in
  // the destination graph already exist
  //
  depends_t::edge_iterator ei, ei_end;
  for (tie(ei, ei_end) = boost::edges(from); ei != ei_end; ++ei) {
    depends_vertex_t src = boost::source(*ei, from);
    depends_vertex_t dst = boost::target(*ei, from);

    auto src_intervl(
        boost::icl::discrete_interval<source_location_t>::right_open(
            from[src].beg, from[src].end));
    auto dst_intervl(
        boost::icl::discrete_interval<source_location_t>::right_open(
            from[dst].beg, from[dst].end));

    auto &map_for_src =
        is_system_source_file(from[src].f)
            ? into_syst_sl_vert_map[index_of_source_file(from[src].f)]
            : into_user_sl_vert_map[index_of_source_file(from[src].f)];

    auto &map_for_dst =
        is_system_source_file(from[dst].f)
            ? into_syst_sl_vert_map[index_of_source_file(from[dst].f)]
            : into_user_sl_vert_map[index_of_source_file(from[dst].f)];

    auto it_for_src = map_for_src.find(src_intervl);
    auto it_for_dst = map_for_dst.find(dst_intervl);

    assert(it_for_src != map_for_src.end());
    assert(it_for_dst != map_for_dst.end());

    depends_vertex_t to_src = *(*it_for_src).second.begin();
    depends_vertex_t to_dst = *(*it_for_dst).second.begin();

    // parallel edges are voided due to set container being used
    if (boost::edge(to_dst, to_src, into).second)
      continue;

    into[boost::add_edge(to_src, to_dst, into).first].t = from[*ei].t;
  }

  //
  // add globals from given graph to destination graph
  //
  for (auto &entry : from[boost::graph_bundle].glbl_defs) {
#if 0
    auto into_it = into[boost::graph_bundle].glbl_defs.find(entry.first);
    if (into_it != into[boost::graph_bundle].glbl_defs.end()) {
#if 0
      cerr << "warning: multiple definitions found for '" << entry.first << '\''
           << endl;
#else
           continue;
#endif
    }
#endif

#if 0
    cerr << "GlobalDefinition '" << entry.first << "' "
         << (is_system_source_file(entry.second.f)
                 ? into[boost::graph_bundle].syst_src_f_paths.at(
                       index_of_source_file(entry.second.f))
                 : into[boost::graph_bundle].user_src_f_paths.at(
                       index_of_source_file(entry.second.f)))
         << ':' << is_system_source_file(entry.second.f) << ':'
         << entry.second.beg << ':' << entry.second.end << " ("
         << into[boost::graph_bundle].syst_src_f_paths.size() << ", "
         << into[boost::graph_bundle].user_src_f_paths.size() << ')'
         << std::endl;
#endif

    into[boost::graph_bundle].glbl_defs[entry.first] = entry.second;
  }

  for (auto &entry : from[boost::graph_bundle].glbl_decls) {
    for (auto &sl_entry : entry.second) {

#if 0
      cerr << "GlobalDeclaration '" << entry.first << "' "
           << (is_system_source_file(sl_entry.f)
                   ? into[boost::graph_bundle].syst_src_f_paths.at(
                         index_of_source_file(sl_entry.f))
                   : into[boost::graph_bundle].user_src_f_paths.at(
                         index_of_source_file(sl_entry.f)))
           << ':' << is_system_source_file(sl_entry.f) << ':' << sl_entry.beg
           << ':' << sl_entry.end << " ("
           << into[boost::graph_bundle].syst_src_f_paths.size() << ", "
           << into[boost::graph_bundle].user_src_f_paths.size() << ')'
           << std::endl;
#endif

      into[boost::graph_bundle].glbl_decls[entry.first].insert(sl_entry);
    }
  }

  //
  // add static functions from given graph to destination graph
  //
  for (auto &entry : from[boost::graph_bundle].static_defs)
    for (auto &sl_entry : entry.second)
      into[boost::graph_bundle].static_defs[entry.first].insert(sl_entry);

  for (auto &entry : from[boost::graph_bundle].static_decls)
    for (auto &sl_entry : entry.second)
      into[boost::graph_bundle].static_decls[entry.first].insert(sl_entry);
}

void resolve_references(
    depends_t &out,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &user_sl_vert_map,
    vector<boost::icl::interval_map<source_location_t, set<depends_vertex_t>>>
        &syst_sl_vert_map) {
  //
  // for every global declaration, make a dummy vertex for which all of them
  // have a forward declaration edge to, and then add an edge from that dummy
  // vertex to the corresponding definition vertex for those declarations
  //
  for (auto &entry : out[boost::graph_bundle].glbl_decls) {
    // does a corresponding definition exist?
    auto def_it = out[boost::graph_bundle].glbl_defs.find(entry.first);
    if (def_it == out[boost::graph_bundle].glbl_defs.end())
      continue;

    // get global definition vertex
    auto def_sr = (*def_it).second;
    auto &def_sr_map = is_system_source_file(def_sr.f)
                           ? syst_sl_vert_map[index_of_source_file(def_sr.f)]
                           : user_sl_vert_map[index_of_source_file(def_sr.f)];
    auto def_vert_it = def_sr_map.find(def_sr.beg);
    if (def_vert_it == def_sr_map.end()) {
      cerr << "warning (bug): global definition not found in source ranges map "
              "for given declaration" << endl;
      cerr << "symbol: " << (*def_it).first << endl;
      cerr << "offset: " << def_sr.beg << endl;
      cerr << "file: "
           << (is_system_source_file(def_sr.f)
                   ? out[boost::graph_bundle]
                         .syst_src_f_paths[index_of_source_file(def_sr.f)]
                   : out[boost::graph_bundle]
                         .user_src_f_paths[index_of_source_file(def_sr.f)])
           << endl;
      continue;
    }
    auto def_vert = *(*def_vert_it).second.begin();

    // make dummy vertex
    auto dummy_vert = boost::add_vertex(out);
    out[dummy_vert].beg = location_dummy_beg;
    out[dummy_vert].end = location_dummy_end;

    // for every declaration vertex, add an edge from it to the dummy vertex
    for (auto &dcl_sr : entry.second) {
      auto &dcl_sr_map =
          (is_system_source_file(dcl_sr.f)
               ? syst_sl_vert_map[index_of_source_file(dcl_sr.f)]
               : user_sl_vert_map[static_cast<unsigned>(dcl_sr.f)]);

      auto dcl_vert_it = dcl_sr_map.find(dcl_sr.beg);
      if (dcl_vert_it == dcl_sr_map.end()) {
        cerr << "warning (bug): global declaration not found in source ranges "
                "map"
             << endl;
        continue;
      }

      auto dcl_vert = *(*dcl_vert_it).second.begin();
      out[boost::add_edge(dcl_vert, dummy_vert, out).first].t =
          DEPENDS_FWD_DECL_EDGE;
    }

    // connect dummy vertex
    out[boost::add_edge(dummy_vert, def_vert, out).first].t =
        DEPENDS_FWD_DECL_EDGE;
  }

#if 0
  //
  // static functions are tricky. in the event that two static functions are
  // defined with the same name, and the extracted code depends on both, we have
  // to make a choice: either present the user with code that doesn't compile
  // due to a symbol multiply error, or only choose one of the static functions
  // with the foreknowledge that any static functions with the same name will be
  // equivalent; both in type and semantics.
  //

  //
  // bring in only one definition of any static function with the same name. we
  // proceed as follows.
  //
  // for every set of static functions with a given name, we arbitrarily pick a
  // definition among one of them.
  //
  // for all code which depends on any static function definition with the same
  // name: make them depend on the previously picked static definition instead.
  //
  // for all code which depends on any static function declaration with the same
  // name, make a dummy vertex for which all of them have a forward declaration
  // edge to, and then add an edge from that dummy vertex to the definition
  // vertex corresponding to the static function definition that we arbitrarily
  // chose.
  //

  //
  // for all code which depends on a static function declaration, search for a
  // static function definition in *the same file* as the code which is the
  // dependent. add a forward declaration edge from the dependent to the static
  // function definition.
  //
  // if such a static function definition cannot be found, then we're in
  // trouble. print a warning.
  //

  for (auto &family_of_static_def : out[boost::graph_bundle].static_defs) {
    const string& defs_nm = family_of_static_def.first;
    const list<full_source_location_t>& defs = family_of_static_def.second;

    assert(defs.begin() != defs.end());

    auto it = defs.begin();

    // arbitrarily chosen definition for this family of static functions with
    // the same name
    full_source_location_t def = *it++;

    auto &def_sr_map = is_system_source_file(def.f)
                           ? syst_sl_vert_map[index_of_source_file(def.f)]
                           : user_sl_vert_map[index_of_source_file(def.f)];
    auto def_vert_it = def_sr_map.find(def.beg);
    if (def_vert_it == def_sr_map.end()) {
      cerr << "warning (bug): definition for static function not found in "
              "source ranges map for "
           << defs_nm << endl;
      cerr << "offset: " << def.beg << endl;
      cerr << "file: "
           << (is_system_source_file(def.f)
                   ? out[boost::graph_bundle]
                         .syst_src_f_paths[index_of_source_file(def.f)]
                   : out[boost::graph_bundle]
                         .user_src_f_paths[index_of_source_file(def.f)])
           << endl;

      continue;
    }
    auto def_vert = *(*def_vert_it).second.begin();

    while (it != defs.end()) {
      full_source_location_t other_def = *it++;

      // get vertex for this other definition
      auto &other_def_sr_map =
          is_system_source_file(other_def.f)
              ? syst_sl_vert_map[index_of_source_file(other_def.f)]
              : user_sl_vert_map[index_of_source_file(other_def.f)];
      auto other_def_vert_it = other_def_sr_map.find(other_def.beg);
      if (other_def_vert_it == other_def_sr_map.end()) {
        cerr << "warning (bug): definition for static function not found in "
                "source ranges map for "
             << defs_nm << endl;
        cerr << "offset: " << other_def.beg << endl;
        cerr << "file: "
             << (is_system_source_file(other_def.f)
                     ? out[boost::graph_bundle]
                           .syst_src_f_paths[index_of_source_file(other_def.f)]
                     : out[boost::graph_bundle]
                           .user_src_f_paths[index_of_source_file(other_def.f)])
             << endl;

        continue;
      }
      auto other_def_vert = *(*other_def_vert_it).second.begin();

      depends_t::in_edge_iterator e_it, e_it_end;
      tie(e_it, e_it_end) = boost::in_edges(other_def_vert, out);
      vector<depends_vertex_t> deps;
      transform(e_it, e_it_end, back_inserter(deps),
                [&](depends_edge_t e) -> depends_vertex_t {
                  return boost::source(e, out);
                });

      boost::clear_in_edges(other_def_vert, out);

      for (auto dep : deps)
        boost::add_edge(dep, def_vert, out);
    }

    // do corresponding declarations exist?
    auto decls_it = out[boost::graph_bundle].static_decls.find(defs_nm);
    if (decls_it == out[boost::graph_bundle].static_decls.end())
      continue;

    // make dummy vertex
    auto dummy_vert = boost::add_vertex(out);
    out[dummy_vert].beg = location_dummy_beg;
    out[dummy_vert].end = location_dummy_end;

    // for every declaration vertex, add an edge from it to the dummy vertex
    for (auto& decl : (*decls_it).second) {
      auto &decl_sr_map =
          (is_system_source_file(decl.f)
               ? syst_sl_vert_map[index_of_source_file(decl.f)]
               : user_sl_vert_map[decl.f]);

      auto decl_vert_it = decl_sr_map.find(decl.beg);
      if (decl_vert_it == decl_sr_map.end()) {
        cerr << "warning (bug): global declaration not found in source ranges "
                "map"
             << endl;
        continue;
      }

      auto decl_vert = *(*decl_vert_it).second.begin();
      out[boost::add_edge(decl_vert, dummy_vert, out).first].t =
          DEPENDS_FWD_DECL_EDGE;
    }

    // connect dummy vertex
    out[boost::add_edge(dummy_vert, def_vert, out).first].t =
        DEPENDS_FWD_DECL_EDGE;
  }
#endif
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

}
