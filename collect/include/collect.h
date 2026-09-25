#pragma once
#include "fsuid.h"

#include <memory>
#include <string>
#include <ostream>
#include <list>
#include <set>
#include <boost/filesystem.hpp>

#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>

namespace carbon {

// byte offset into source file
typedef int32_t clang_source_location_t;

// opaque structure uniquely identifying a file in clang
typedef clang::FileID clang_source_file_t;

// defined in clang_collect.cpp
std::size_t hash_of_clang_source_file(const clang_source_file_t &);

// defined in clang_collect.cpp
struct failed_to_get_path_exception {};
boost::filesystem::path path_of_clang_source_file(clang::FileEntryRef);
boost::filesystem::path path_of_clang_source_file(const clang_source_file_t &);

// these functions are defined in clang_collect.cpp
bool clang_is_system_source_file(const clang_source_file_t &);
bool clang_is_system_source_file(clang::FileEntryRef);
FSUniqueID clang_fs_unique_id(const llvm::sys::fs::UniqueID &);

struct clang_full_source_location_t {
  clang_source_file_t f;
  clang_source_location_t pos;

  bool operator<(const clang_full_source_location_t &other) const {
    return f < other.f || pos < other.pos;
  }
};

struct clang_source_range_t {
  clang_source_file_t f;
  clang_source_location_t beg;
  clang_source_location_t end;

  explicit operator bool(void) const {
    return end > beg && beg >= 0;
  }

  clang_full_source_location_t getBegin(void) const { return {f, beg}; }
  clang_full_source_location_t getEnd(void)   const { return {f, end}; }
};

// defined in carbon_collect.cpp
char character_at_clang_file_offset(const clang_source_file_t &f,
                                    const clang_source_location_t &o);
unsigned char_count_until_semicolon(const clang_source_range_t &cl_src_rng);
unsigned char_count(const clang_source_file_t &);

// defined in carbon_collect.cpp
clang_source_file_t top_level_system_header(const clang_source_file_t &);

struct collector_priv;
class collector {
  boost::filesystem::path srcfp;
  boost::filesystem::path root_src_dir;
  boost::filesystem::path root_bin_dir;

  std::unique_ptr<collector_priv> priv;

public:
  collector();
  ~collector();

  void set_args(const boost::filesystem::path& srcfp,
                const boost::filesystem::path& root_src_dir,
                const boost::filesystem::path& root_bin_dir);
  void set_invocation_macros(
      const std::vector<std::pair<std::string, bool /*isUndef*/>> &Macros);
  void
  set_invocation_header_directories(const std::set<std::string> &);

  void code(const clang_source_range_t &);
  void global_code(const clang_source_range_t &, const std::string &sym,
                   bool is_definition);
  void static_code(const clang_source_range_t &, const std::string &sym,
                   bool is_definition);

  void use(const clang_source_range_t &user, const clang_source_range_t &usee);
  void use_if_user_exists(const clang_source_range_t &user,
                          const clang_source_range_t &usee);
  void follow_users_of(const clang_source_range_t &prior,
                       const clang_source_range_t &following);

  void inclusion(const clang_full_source_location_t &include,
                 clang::FileEntryRef included_f);

  void ifdef(const clang_full_source_location_t &user,
             const clang_source_range_t &usee);

  void ifndef(const clang_full_source_location_t &user,
              const clang_source_range_t &usee);

  void defined(const clang_full_source_location_t &user,
               const clang_source_range_t &usee);

  void clang_source_file(const clang_source_file_t &);
  void clang_source_file(clang::FileEntryRef);

  // process #include's, #ifdef's, #ifndef's, and defined's
  void finalize(void);
  void write_carbon_output();
};

}
