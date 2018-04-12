#include "toposort.h"
#include "graphviz_impl.h"
#include <collect_impl.h>
#include <map>
#include <iostream>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/strong_components.hpp>

using namespace std;

namespace carbon {

struct topo_edges_excluding_some {
  const depends_t *g_ptr;
  const unordered_map<depends_vertex_t, unordered_set<depends_vertex_t>>
      *bad_edges;

  topo_edges_excluding_some() : g_ptr(nullptr) {}
  topo_edges_excluding_some(
      const depends_t *g_ptr,
      const unordered_map<depends_vertex_t, unordered_set<depends_vertex_t>>
          *bad_edges)
      : g_ptr(g_ptr), bad_edges(bad_edges) {}

  template <typename Edge> bool operator()(const Edge &e) const {
    const depends_t &g = *g_ptr;
    if (!(g[e].t != DEPENDS_FWD_DECL_EDGE))
      return false;

    auto src_it = bad_edges->find(boost::source(e, g));
    if (src_it == bad_edges->end())
      return true;

    auto tar_it = (*src_it).second.find(boost::target(e, g));
    if (tar_it == (*src_it).second.end())
      return true;

    return false;
  }
};

struct topo_edges {
  const depends_t *g_ptr;

  topo_edges() : g_ptr(nullptr) {}
  topo_edges(const depends_t *g_ptr) : g_ptr(g_ptr) {}

  template <typename Edge> bool operator()(const Edge &e) const {
    const depends_t &g = *g_ptr;
    return g[e].t != DEPENDS_FWD_DECL_EDGE;
  }
};

struct vert_exists_in_set {
  unordered_set<depends_vertex_t> *s;

  vert_exists_in_set() : s(nullptr) {}
  vert_exists_in_set(unordered_set<depends_vertex_t> *s) : s(s) {}
  bool operator()(const depends_vertex_t &v) const {
    return s->find(v) != s->end();
  }
};

void topologically_sort_code(std::list<code_t> &out, const depends_t &g) {
  topo_edges e_filter(&g);
  boost::filtered_graph<depends_t, topo_edges> fg(g, e_filter);

  cerr << "topologically sorting dependency graph..." << endl;

  try {
    map<depends_vertex_t, int> idx_map;

    int i = 0;
    depends_t::vertex_iterator vi, vi_end;
    for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi)
      idx_map[*vi] = i++;

    map<depends_vertex_t, boost::default_color_type> clr_map;

    boost::topological_sort(
        fg, back_inserter(out),
        boost::color_map(
            boost::associative_property_map<
                map<depends_vertex_t, boost::default_color_type>>(clr_map))
            .vertex_index_map(
                boost::associative_property_map<map<depends_vertex_t, int>>(
                    idx_map)));
  } catch (const boost::not_a_dag &) {
    cerr << "warning (bug): cycle(s) found in dependency graph" << endl;

    //
    // write dot file for every strongly connected components
    //
    map<depends_vertex_t, depends_t::vertices_size_type> vert_comps;

    {
      map<depends_vertex_t, int> tm_map;
      map<depends_vertex_t, depends_vertex_t> rt_map;
      map<depends_vertex_t, boost::default_color_type> clr_map;

      map<depends_vertex_t, int> idx_map;

      int i = 0;
      depends_t::vertex_iterator vi, vi_end;
      for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi)
        idx_map[*vi] = i++;

      boost::strong_components(
          fg,
          boost::associative_property_map<
              map<depends_vertex_t, depends_t::vertices_size_type>>(vert_comps),
          boost::root_map(boost::associative_property_map<
                              map<depends_vertex_t, depends_vertex_t>>(rt_map))
              .discover_time_map(
                  boost::associative_property_map<map<depends_vertex_t, int>>(
                      tm_map))
              .color_map(boost::associative_property_map<
                         map<depends_vertex_t, boost::default_color_type>>(
                  clr_map))
              .vertex_index_map(
                  boost::associative_property_map<map<depends_vertex_t, int>>(
                      idx_map)));
    }

    //
    // write graphviz files for every strongly connected component
    //
    map<depends_t::vertices_size_type, unordered_set<depends_vertex_t>>
        comp_verts_map;
    for (map<depends_vertex_t, depends_t::vertices_size_type>::value_type el :
         vert_comps)
      comp_verts_map[el.second].insert(el.first);

    unordered_map<depends_vertex_t, unordered_set<depends_vertex_t>> bad_edges;
    for (map<depends_t::vertices_size_type,
             unordered_set<depends_vertex_t>>::value_type comp_verts :
         comp_verts_map) {
      if (comp_verts.second.size() == 1)
        continue;

      topo_edges _e_filter(&g);
      vert_exists_in_set v_filter(&comp_verts.second);
      boost::filtered_graph<depends_t, topo_edges, vert_exists_in_set> fg_(
          g, _e_filter, v_filter);

      //
      // also (while we are doing this) keep track of every edge between the
      // vertices in a strongly connected component- we'll forcefully exclude
      // them from the topological sort later on.
      //
      auto e_it_pair = boost::edges(fg_);
      auto e_it = e_it_pair.first;
      auto e_it_end = e_it_pair.second;
      for (; e_it != e_it_end; ++e_it)
        bad_edges[boost::source(*e_it, fg_)].insert(boost::target(*e_it, fg_));

      ostringstream dotfp;
      dotfp << "/tmp/carbon.scc." << dec << comp_verts.first << ".dot";

      cerr << "writing scc of size " << comp_verts.second.size()
           << " vertices (" << dotfp.str() << ')' << endl;

      ofstream dotf(dotfp.str(), ofstream::out);
      code_reader cr(g);
      output_graphviz(dotf, cr, fg_);
    }

    //
    // excluding the edges which induce cycles, re-topologically sort
    //
    out.clear();
    {
      map<depends_vertex_t, int> idx_map;

      int i = 0;
      depends_t::vertex_iterator vi, vi_end;
      for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi)
        idx_map[*vi] = i++;

      map<depends_vertex_t, boost::default_color_type> clr_map;

      topo_edges_excluding_some _e_filter(&g, &bad_edges);
      boost::filtered_graph<depends_t, topo_edges_excluding_some> fg_(
          g, _e_filter);

      boost::topological_sort(
          fg_, back_inserter(out),
          boost::color_map(
              boost::associative_property_map<
                  map<depends_vertex_t, boost::default_color_type>>(clr_map))
              .vertex_index_map(
                  boost::associative_property_map<map<depends_vertex_t, int>>(
                      idx_map)));
    }
  }

  cerr << "topologically sorted dependency graph." << endl;
}
}
