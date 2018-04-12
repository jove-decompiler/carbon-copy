#pragma once
#include <boost/graph/adjacency_list.hpp>
#include <string>

namespace carbon {

// byte offset into source file
typedef int32_t source_location_t;

// if positive, is index into sources
// if negative, negation is index into system sources
typedef int32_t source_file_t;

inline bool is_system_source_file(const source_file_t &f) {
  return f < 0;
}
inline unsigned index_of_source_file(const source_file_t &f) {
  return static_cast<unsigned>(is_system_source_file(f) ? -(f + 1) : f);
}
inline source_file_t syst_index_of_index(unsigned idx) {
  return -1 - static_cast<source_file_t>(idx);
}

static const source_location_t location_dummy_beg = INT32_MIN;
static const source_location_t location_dummy_end = INT32_MIN + 1;

static const source_location_t location_entire_file_beg = INT32_MAX - 1;
static const source_location_t location_entire_file_end = INT32_MAX;

// pair of source locations, and which file they reside in
// since source ranges never overlap source_range_uid_t can uniquely identify
struct source_range_t {
  source_file_t f;
  source_location_t beg;
  source_location_t end;

  template <class Archive>
  void serialize(Archive &ar, const unsigned int) {
    ar &f &beg &end;
  }
};

struct full_source_location_t {
  source_file_t f;
  source_location_t beg;

  bool operator<(const full_source_location_t &sl) const {
    return f < sl.f || beg < sl.beg;
  }

  template <class Archive>
  void serialize(Archive &ar, const unsigned int) {
    ar &f &beg;
  }
};

enum DEPENDS_EDGE_TYPE {
  DEPENDS_NORMAL_EDGE,
  DEPENDS_FWD_DECL_EDGE,
  DEPENDS_FOLLOWS_EDGE
};

struct depends_edge_type_t {
  DEPENDS_EDGE_TYPE t;
  depends_edge_type_t() : t(DEPENDS_NORMAL_EDGE) {}

  template <class Archive>
  void serialize(Archive &ar, const unsigned int) {
    ar &t;
  }
};

struct depends_context_t {
  std::unordered_map<std::string, full_source_location_t> glbl_defs;
  std::unordered_map<std::string, std::set<full_source_location_t>> glbl_decls;

  std::unordered_map<std::string, std::set<full_source_location_t>>
      static_defs;
  std::unordered_map<std::string, std::set<full_source_location_t>>
      static_decls;

  std::vector<std::string> user_src_f_paths;

  std::vector<std::string> syst_src_f_paths;

  /* parallel to syst_src_f_paths, this contains the "top-level" headers
   * which user code referenced which eventually included the corresponding
   * headers */
  std::vector<std::string> toplvl_syst_src_f_paths;

  struct {
    std::set<std::string> def, und;
  } macros;

  struct {
    std::set<std::string> dirs;
  } include;

  template <class Archive>
  void serialize(Archive &ar, const unsigned int) {
    ar &glbl_defs &glbl_decls &static_defs &static_decls &user_src_f_paths
        &syst_src_f_paths &toplvl_syst_src_f_paths &macros.def &macros
            .und &include.dirs;
  }
};

typedef boost::adjacency_list<
    boost::setS, /* no parallel edges */
    boost::listS,
    boost::bidirectionalS, /* directed graph (with in and out edges) */
    source_range_t, depends_edge_type_t, depends_context_t>
    depends_t;

typedef depends_t::vertex_descriptor depends_vertex_t;
typedef depends_t::edge_descriptor depends_edge_t;
}
