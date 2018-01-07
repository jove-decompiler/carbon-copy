#pragma once
#include "code_reader.h"
#include <collect_impl.h>
#include <map>
#include <boost/graph/graphviz.hpp>

namespace carbon {

template <typename Graph> struct graphviz_label_writer {
  const Graph &g;
  code_reader &cr;

  graphviz_label_writer(const Graph &g, code_reader &cr) : g(g), cr(cr) {}

  template <class VertexOrEdge>
  void operator()(std::ostream &out, const VertexOrEdge &v) const {
    std::string src;

#if 0
    std::cerr << g[v].f << ':' << is_system_source_file(g[v].f) << " ("
              << g[boost::graph_bundle].syst_src_f_paths.size() << ", "
              << g[boost::graph_bundle].user_src_f_paths.size() << ')'
              << std::endl;
#endif

    if (g[v].beg == location_dummy_beg &&
        g[v].end == location_dummy_end)
      // dummy vertex
      src = "<dummy>";
    else if (g[v].beg == location_entire_file_beg &&
             g[v].end == location_entire_file_end)
      // 'entire file' vertex
      src = is_system_source_file(g[v].f)
                ? g[boost::graph_bundle].syst_src_f_paths.at(
                      index_of_source_file(g[v].f))
                : g[boost::graph_bundle].user_src_f_paths.at(
                      index_of_source_file(g[v].f));
    else
      // normal vertex
      src = cr.source_text(v);

#if 0
      src = is_system_source_file(g[v].f)
                ? g[boost::graph_bundle]
                      .syst_src_f_paths.at(index_of_source_file(g[v].f))
                : g[boost::graph_bundle]
                      .user_src_f_paths.at(index_of_source_file(g[v].f));
      src += ":";
      src += std::to_string(g[v].beg);
      src += ":";
      src += std::to_string(g[v].end);
#endif

    src.reserve(2 * src.size());

    boost::replace_all(src, "\\", "\\\\");
    boost::replace_all(src, "\r\n", "\\l");
    boost::replace_all(src, "\n", "\\l");
    boost::replace_all(src, "\"", "\\\"");
    boost::replace_all(src, "{", "\\{");
    boost::replace_all(src, "}", "\\}");
    boost::replace_all(src, "|", "\\|");
    boost::replace_all(src, "|", "\\|");
    boost::replace_all(src, "<", "\\<");
    boost::replace_all(src, ">", "\\>");
    boost::replace_all(src, "(", "\\(");
    boost::replace_all(src, ")", "\\)");
    boost::replace_all(src, ",", "\\,");
    boost::replace_all(src, ";", "\\;");
    boost::replace_all(src, ":", "\\:");
    boost::replace_all(src, " ", "\\ ");

    out << "[label=\"";
    out << src;
    out << "\"]";
  }
};

template <typename Graph> struct graphviz_edge_prop_writer {
  const Graph &g;
  graphviz_edge_prop_writer(const Graph &g) : g(g) {}

  template <class Edge>
  void operator()(std::ostream &out, const Edge &e) const {
    static const char *edge_type_styles[] = {
        "solid", "dashed", /*"invis"*/ "dotted"
    };

    out << "[style=\"" << edge_type_styles[g[e].t] << "\"]";
  }
};

struct graphviz_prop_writer {
  void operator()(std::ostream &out) const {
    out << "fontname = \"Courier\"\n"
           "fontsize = 10\n"
           "\n"
           "node [\n"
           "fontname = \"Courier\"\n"
           "fontsize = 10\n"
           "shape = \"record\"\n"
           "]\n"
           "\n"
           "edge [\n"
           "fontname = \"Courier\"\n"
           "fontsize = 10\n"
           "]\n"
           "\n";
  }
};

template <typename Graph>
void output_graphviz(std::ostream &out, code_reader &cr, const Graph &g) {
  typedef typename Graph::vertex_descriptor vertex_t;
  typedef typename Graph::vertex_iterator vertex_iterator_t;

  std::map<vertex_t, int> idx_map;

  int i = 0;
  vertex_iterator_t vi, vi_end;
  for (tie(vi, vi_end) = boost::vertices(g); vi != vi_end; ++vi)
    idx_map[*vi] = i++;

#if 0
  std::cerr << "-----------------------------" << std::endl;
#endif
  boost::write_graphviz(
      out, g, graphviz_label_writer<Graph>(g, cr),
      graphviz_edge_prop_writer<Graph>(g), graphviz_prop_writer(),
      boost::associative_property_map<std::map<vertex_t, int>>(idx_map));
}
}
