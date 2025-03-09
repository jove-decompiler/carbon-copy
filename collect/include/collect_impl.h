#pragma once
#include <boost/graph/adjacency_list.hpp>
#include <boost/container/scoped_allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>
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

  std::set<std::pair<unsigned, source_file_t>> includes;

  template <class Archive>
  void serialize(Archive &ar, const unsigned int) {
    ar &f &beg &end &includes;
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

//
// symbol table (.cc)
//

typedef boost::interprocess::managed_mapped_file cc_file_t;
typedef cc_file_t::segment_manager segment_manager_t;

typedef boost::interprocess::allocator<char, segment_manager_t>
    ip_char_allocator;
typedef boost::interprocess::basic_string<char, std::char_traits<char>,
                                          ip_char_allocator>
    ip_string;

struct ip_string_hash_t  {
  using is_transparent = void;

  template <typename A>
  std::size_t operator()(
      const boost::interprocess::basic_string<char, std::char_traits<char>, A>
          &str) const noexcept {
    return std::hash<std::string_view>{}(std::string_view(str.data(), str.size()));
  }

  std::size_t operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
  }

  std::size_t operator()(const char *s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};

struct ip_string_equal_t {
  using is_transparent = void;

  template <typename A>
  bool operator()(
      const boost::interprocess::basic_string<char, std::char_traits<char>, A> &lhs,
      const boost::interprocess::basic_string<char, std::char_traits<char>, A> &rhs) const noexcept {
    return lhs == rhs;
  }

  template <typename A>
  bool operator()(const boost::interprocess::basic_string<char, std::char_traits<char>, A> &lhs,
      std::string_view rhs) const noexcept {
    return lhs == rhs;
  }

  template <typename A>
  bool operator()(std::string_view lhs,
                  const boost::interprocess::basic_string<char, std::char_traits<char>, A> &rhs) const noexcept {
    return lhs == rhs;
  }

  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return lhs == rhs;
  }

  template <typename A>
  bool operator()(const boost::interprocess::basic_string<char, std::char_traits<char>, A> &lhs,
      const char *rhs) const noexcept {
    return lhs == rhs;
  }

  template <typename A>
  bool operator()(
      const char *lhs,
      const boost::interprocess::basic_string<char, std::char_traits<char>, A> &rhs) const noexcept {
    return lhs == rhs;
  }

  bool operator()(std::string_view lhs, const char *rhs) const noexcept {
    return lhs == rhs;
  }

  bool operator()(const char *lhs, std::string_view rhs) const noexcept {
    return lhs == rhs;
  }
};

static inline std::string un_ips(const ip_string &x) {
  std::string res;
  res.reserve(x.size());
  std::copy(x.begin(), x.end(), std::back_inserter(res));
  return res;
}

static inline ip_string &to_ips(ip_string &res, std::string_view x) {
  res.clear();
  res.reserve(x.size());
  std::copy(x.begin(), x.end(), std::back_inserter(res));
  return res;
}

using cc_carbs_t = boost::concurrent_flat_set<
    ip_string, ip_string_hash_t, ip_string_equal_t,
    boost::container::scoped_allocator_adaptor<
        boost::interprocess::allocator<ip_string, segment_manager_t>>>;

using cc_syms_t = boost::concurrent_flat_map<
    ip_string, cc_carbs_t, ip_string_hash_t, ip_string_equal_t,
    boost::container::scoped_allocator_adaptor<boost::interprocess::allocator<
        std::pair<const ip_string, cc_carbs_t>, segment_manager_t>>>;
}
