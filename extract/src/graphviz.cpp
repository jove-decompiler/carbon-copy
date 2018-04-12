#include "graphviz.h"
#include "graphviz_impl.h"

using namespace std;

namespace carbon {

void output_graphviz_of_collection(ostream &out, code_reader &cr,
                                   const depends_t &g) {
  output_graphviz(out, cr, g);
}

struct vert_in_set_filter {
  const unordered_set<code_t> *s;

  vert_in_set_filter() : s(nullptr) {}
  vert_in_set_filter(const unordered_set<code_t> *s) : s(s) {}
  bool operator()(const depends_vertex_t &v) const {
    return s->find(v) != s->end();
  }
};

void output_graphviz_of_reachable_code(std::ostream &out, code_reader &cr,
                                       const unordered_set<code_t> &code,
                                       const depends_t &g) {
  boost::keep_all e_filter;
  vert_in_set_filter v_filter(&code);

  boost::filtered_graph<depends_t, boost::keep_all, vert_in_set_filter> fg(
      g, e_filter, v_filter);

  output_graphviz(out, cr, fg);
}

struct user_vert_filter {
  const depends_t *g;

  user_vert_filter() : g(nullptr) {}
  user_vert_filter(const depends_t *g) : g(g) {}
  bool operator()(const depends_vertex_t &v) const {
    return !is_system_code(*g, v);
  }
};

void output_graphviz_of_user_code(std::ostream &out, code_reader &cr,
                                  const depends_t &g) {
  boost::keep_all e_filter;
  user_vert_filter v_filter(&g);

  boost::filtered_graph<depends_t, boost::keep_all, user_vert_filter> fg(
      g, e_filter, v_filter);

  output_graphviz(out, cr, fg);
}

struct user_vert_in_set_filter {
  const depends_t *g;
  const unordered_set<code_t> *s;

  user_vert_in_set_filter() : g(nullptr), s(nullptr) {}
  user_vert_in_set_filter(const depends_t *g, const unordered_set<code_t> *s)
      : g(g), s(s) {}

  bool operator()(const depends_vertex_t &v) const {
    return !is_system_code(*g, v) && s->find(v) != s->end();
  }
};

void output_graphviz_of_reachable_user_code(std::ostream &out, code_reader &cr,
                                            const unordered_set<code_t> &code,
                                            const depends_t &g) {
  boost::keep_all e_filter;
  user_vert_in_set_filter v_filter(&g, &code);

  boost::filtered_graph<depends_t, boost::keep_all, user_vert_in_set_filter> fg(
      g, e_filter, v_filter);

  output_graphviz(out, cr, fg);
}
}
