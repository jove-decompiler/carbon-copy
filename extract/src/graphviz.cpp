#include "graphviz.h"
#include "graphviz_impl.h"
#include "collection_impl.h"

using namespace std;

namespace carbon {

void output_graphviz_of_collection(ostream &out, code_reader &cr,
                                   const collection_t &clc) {
  output_graphviz(out, cr, clc.g);
}

struct vert_in_set_filter {
  const unordered_set<depends_vertex_t> *s;

  vert_in_set_filter() : s(nullptr) {}
  vert_in_set_filter(const unordered_set<depends_vertex_t> *s) : s(s) {}
  bool operator()(const depends_vertex_t &v) const {
    return s->find(v) != s->end();
  }
};

void output_graphviz_of_reachable_code(std::ostream &out, code_reader &cr,
                                       const unordered_set<code_t> &code,
                                       const collection_t &clc) {
  boost::keep_all e_filter;
  vert_in_set_filter v_filter(&code);

  boost::filtered_graph<depends_t, boost::keep_all, vert_in_set_filter> fg(
      clc.g, e_filter, v_filter);

  output_graphviz(out, cr, fg);
}

struct user_vert_filter {
  const collection_t *clc;

  user_vert_filter() : clc(nullptr) {}
  user_vert_filter(const collection_t *clc) : clc(clc) {}
  bool operator()(const depends_vertex_t &v) const {
    return !is_system_code(*clc, v);
  }
};

void output_graphviz_of_user_code(std::ostream &out, code_reader &cr,
                                  const collection_t &clc) {
  boost::keep_all e_filter;
  user_vert_filter v_filter(&clc);

  boost::filtered_graph<depends_t, boost::keep_all, user_vert_filter> fg(
      clc.g, e_filter, v_filter);

  output_graphviz(out, cr, fg);
}

struct user_vert_in_set_filter {
  const collection_t *clc;
  const unordered_set<depends_vertex_t> *s;

  user_vert_in_set_filter() : clc(nullptr), s(nullptr) {}
  user_vert_in_set_filter(const collection_t *clc,
                          const unordered_set<depends_vertex_t> *s)
      : clc(clc), s(s) {}
  bool operator()(const depends_vertex_t &v) const {
    return !is_system_code(*clc, v) && s->find(v) != s->end();
  }
};

void output_graphviz_of_reachable_user_code(std::ostream &out, code_reader &cr,
                                            const unordered_set<code_t> &code,
                                            const collection_t &clc) {
  boost::keep_all e_filter;
  user_vert_in_set_filter v_filter(&clc, &code);

  boost::filtered_graph<depends_t, boost::keep_all, user_vert_in_set_filter> fg(
      clc.g, e_filter, v_filter);

  output_graphviz(out, cr, fg);
}
}
