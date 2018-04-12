#include "code_reader.h"
#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <boost/filesystem.hpp>

using namespace std;
namespace fs = boost::filesystem;

namespace carbon {

static void print_istream_error(const istream& is) {
  if (is.fail())
    cerr << "input/output operation failed- formatting or extraction error "
            "(failbit)"
         << endl;
  else if (is.bad())
    cerr << "irrecoverable stream error (badbit)" << endl;
}

code_reader::code_reader(const depends_t &g) : g(g) {
  const depends_context_t &depctx = g[boost::graph_bundle];

#if 0
  user_files.reserve(depctx.user_src_f_paths.size());
  syst_files.reserve(depctx.syst_src_f_paths.size());

  transform(depctx.user_src_f_paths.begin(), depctx.user_src_f_paths.end(),
            back_inserter(user_files), [](const string &path) {
              unique_ptr<ifstream> res(
                  new ifstream(path, ios::in));
              if (!res) {
                cerr << "error: failed to open source file '" << path << '\''
                     << endl;
                print_istream_error(*res);
                exit(1);
              }
              return res;
            });

  transform(depctx.syst_src_f_paths.begin(), depctx.syst_src_f_paths.end(),
            back_inserter(syst_files), [](const string &path) {
              unique_ptr<ifstream> res(
                  new ifstream(path, ios::in));
              if (!res) {
                cerr << "error: failed to open source file '" << path << '\''
                     << endl;
                print_istream_error(*res);
                exit(1);
              }
              return res;
            });

  user_file_sizes.resize(user_files.size());
  for (unsigned i = 0; i < user_files.size(); ++i) {
    ifstream &is = *user_files[i];
    is.seekg(0, std::ios::end);
    user_file_sizes[i] = is.tellg();
  }

  syst_file_sizes.resize(syst_files.size());
  for (unsigned i = 0; i < syst_files.size(); ++i) {
    ifstream &is = *syst_files[i];
    is.seekg(0, std::ios::end);
    syst_file_sizes[i] = is.tellg();
  }
#else
  user_file_sizes.resize(depctx.user_src_f_paths.size());
  syst_file_sizes.resize(depctx.syst_src_f_paths.size());

  for (unsigned i = 0; i < depctx.user_src_f_paths.size(); ++i) {
    const string& path = depctx.user_src_f_paths[i];

    if (!fs::exists(path)) {
      cerr << "error: failed to open source file '" << path << '\'' << endl;
      exit(1);
    }

    ifstream is(path);
    is.seekg(0, std::ios::end);
    user_file_sizes[i] = static_cast<unsigned>(is.tellg());
  }

  for (unsigned i = 0; i < depctx.syst_src_f_paths.size(); ++i) {
    const string& path = depctx.syst_src_f_paths[i];

    if (!fs::exists(path)) {
      cerr << "error: failed to open source file '" << path << '\'' << endl;
      exit(1);
    }

    ifstream is(path);
    is.seekg(0, std::ios::end);
    syst_file_sizes[i] = static_cast<unsigned>(is.tellg());
  }
#endif
}

code_reader::~code_reader() {}

string code_reader::source_text(code_t c) {
  const source_range_t &src_rng = g[c];

  if (src_rng.beg == location_dummy_beg && src_rng.end == location_dummy_end)
    // dummy vertex
    return "";

  if (src_rng.beg == location_entire_file_beg &&
      src_rng.end == location_entire_file_end)
    // entire file
    return "";

  auto &sizes =
      is_system_source_file(src_rng.f) ? syst_file_sizes : user_file_sizes;
  auto &paths = is_system_source_file(src_rng.f)
                    ? g[boost::graph_bundle].syst_src_f_paths
                    : g[boost::graph_bundle].user_src_f_paths;

  ifstream is(paths.at(index_of_source_file(src_rng.f)));
  if (!is) {
    cerr << "error: could not read "
         << paths.at(index_of_source_file(src_rng.f)) << endl;
    print_istream_error(is);
    exit(1);
  }

  string res;
  res.resize(static_cast<unsigned>(src_rng.end - src_rng.beg));
  is.seekg(static_cast<unsigned>(src_rng.beg) %
           sizes.at(index_of_source_file(src_rng.f)));
  is.read(&res[0], static_cast<ssize_t>(res.size()));

  if (!is) {
    cerr << "error printing code: while trying to read " << res.size()
         << " bytes only " << is.gcount() << " could be read from "
         << debug_source_description(c) << endl;
    print_istream_error(is);
    exit(1);
  }

  return res;
}

string code_reader::debug_source_description(code_t c) {
  const source_range_t &src_rng = g[c];

  auto &paths = is_system_source_file(src_rng.f)
                    ? g[boost::graph_bundle].syst_src_f_paths
                    : g[boost::graph_bundle].user_src_f_paths;
  auto &sizes =
      is_system_source_file(src_rng.f) ? syst_file_sizes : user_file_sizes;

  unsigned n = static_cast<unsigned>(src_rng.end - src_rng.beg);

  unsigned beg = static_cast<unsigned>(src_rng.beg) %
                 sizes.at(index_of_source_file(src_rng.f));
  unsigned end = beg + n;

  ostringstream buff;
  buff << (is_system_source_file(src_rng.f) ? " * " : "")
       << paths.at(index_of_source_file(src_rng.f)) << " [" << src_rng.beg
       << ", " << src_rng.end << ") [" << beg << ", " << end << ')';

  return buff.str();
}

string code_reader::source_description(code_t c) {
  const source_range_t &src_rng = g[c];

  auto &paths = is_system_source_file(src_rng.f)
                    ? g[boost::graph_bundle].syst_src_f_paths
                    : g[boost::graph_bundle].user_src_f_paths;
  auto &sizes =
      is_system_source_file(src_rng.f) ? syst_file_sizes : user_file_sizes;

  unsigned n = static_cast<unsigned>(src_rng.end - src_rng.beg);

  unsigned beg = static_cast<unsigned>(src_rng.beg) % sizes.at(index_of_source_file(src_rng.f));
  unsigned end = beg + n;

  ostringstream buff;
  buff << paths.at(index_of_source_file(src_rng.f)) << " [" << beg << ", "
       << end << ')';

  return buff.str();
}
}
