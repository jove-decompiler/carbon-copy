#pragma once
#include <boost/container/scoped_allocator.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>
#include <boost/interprocess/allocators/node_allocator.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>
#include <boost/unordered/concurrent_node_set.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/unordered/concurrent_node_map.hpp>
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

typedef boost::interprocess::offset_ptr<const char> ip_cstr_t;

struct ip_cstr_hash_t {
  using is_transparent = void;

  std::size_t
  operator()(const ip_cstr_t &x) const noexcept {
    return boost::hash<std::string_view>()(x.get());
  }

  template <typename A>
  std::size_t operator()(
      const boost::interprocess::basic_string<char, std::char_traits<char>, A>
          &x) const noexcept {
    return std::hash<std::string_view>{}(std::string_view(x.data(), x.size()));
  }
};

struct ip_cstr_equal_t {
  using is_transparent = void;

  bool operator()(const ip_cstr_t &lhs,
                  const char *rhs) const noexcept {
    return std::string_view(lhs.get()) == std::string_view(rhs);
  }

  bool operator()(const ip_cstr_t &lhs,
                  const ip_cstr_t &rhs) const noexcept {
    return std::string_view(lhs.get()) == std::string_view(rhs.get());
  }

  template <typename A>
  bool operator()(
      const ip_cstr_t &lhs,
      const boost::interprocess::basic_string<char, std::char_traits<char>, A>
          &rhs) const noexcept {
    return std::string_view(lhs.get()) ==
           std::string_view(rhs.data(), rhs.size());
  }
};

struct ip_string_hash_t  {
  using is_transparent = void;

  template <typename A>
  std::size_t operator()(const boost::interprocess::basic_string<char, std::char_traits<char>, A> &str) const noexcept {
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

  bool operator()(std::string_view lhs, const char* rhs) const noexcept {
    return lhs == rhs;
  }

  bool operator()(const char* lhs, std::string_view rhs) const noexcept {
    return lhs == rhs;
  }
};

using cc_strs_t = boost::concurrent_node_set<
    ip_string, ip_string_hash_t, ip_string_equal_t,
    boost::interprocess::node_allocator<ip_string, segment_manager_t>>;

typedef boost::interprocess::interprocess_sharable_mutex ip_sharable_mutex;

template <typename Mutex>
using ip_sharable_lock = boost::interprocess::sharable_lock<Mutex>;
template <typename Mutex>
using ip_scoped_lock = boost::interprocess::scoped_lock<Mutex>;

struct ip_rw_accessible {
  using mutex_type = ip_sharable_mutex;

  mutable mutex_type mtx;

  using shared_lock_guard = ip_sharable_lock<mutex_type>;
  using exclusive_lock_guard = ip_scoped_lock<mutex_type>;

  shared_lock_guard shared_access() const {
    return shared_lock_guard{mtx};
  }
  exclusive_lock_guard exclusive_access() const {
    return exclusive_lock_guard{mtx};
  }

  ip_rw_accessible() noexcept {}
  ip_rw_accessible(ip_rw_accessible &&) noexcept {}
  ip_rw_accessible &operator=(ip_rw_accessible &&other) noexcept {
    return *this;
  }
  ip_rw_accessible(const ip_rw_accessible &) noexcept {}
  ip_rw_accessible &operator=(const ip_rw_accessible &) noexcept {
    return *this;
  }
};

struct cc_carbs_t : public ip_rw_accessible {
  boost::interprocess::set<
      ip_cstr_t, std::less<ip_cstr_t>,
      boost::interprocess::node_allocator<ip_cstr_t, segment_manager_t>> set;

  cc_carbs_t(segment_manager_t *segment_manager) noexcept
      : set(segment_manager) {}
};

using cc_map_t = boost::concurrent_node_map<
    ip_cstr_t, cc_carbs_t, ip_cstr_hash_t, ip_cstr_equal_t,
    boost::interprocess::node_allocator<std::pair<const ip_cstr_t, cc_carbs_t>,
                                        segment_manager_t>>;

struct cc_syms_t {
  cc_strs_t strs;
  cc_map_t strm;

  cc_syms_t(segment_manager_t *segment_manager) noexcept
      : strs(segment_manager),
        strm(segment_manager) {}
};

}
