#include "code_reader.h"
#include "collection_impl.h"
#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>

using namespace std;

namespace carbon {

struct code_reader_priv {
  const depends_t &g;

#if 0
  vector<unique_ptr<ifstream>> user_files;
  vector<unique_ptr<ifstream>> syst_files;
#endif

  vector<unsigned> user_file_sizes;
  vector<unsigned> syst_file_sizes;

  code_reader_priv(const depends_t &g) : g(g) {}
};

static void print_istream_error(const istream& is) {
  if (is.fail())
    cerr << "input/output operation failed- formatting or extraction error "
            "(failbit)"
         << endl;
  else if (is.bad())
    cerr << "irrecoverable stream error (badbit)" << endl;
}

code_reader::code_reader(const collection_t &g)
    : priv(new code_reader_priv(g.g)) {
  const depends_context_t &depctx = priv->g[boost::graph_bundle];

#if 0
  priv->user_files.reserve(depctx.user_src_f_paths.size());
  priv->syst_files.reserve(depctx.syst_src_f_paths.size());

  transform(depctx.user_src_f_paths.begin(), depctx.user_src_f_paths.end(),
            back_inserter(priv->user_files), [](const string &path) {
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
            back_inserter(priv->syst_files), [](const string &path) {
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

  priv->user_file_sizes.resize(priv->user_files.size());
  for (unsigned i = 0; i < priv->user_files.size(); ++i) {
    ifstream &is = *priv->user_files[i];
    is.seekg(0, std::ios::end);
    priv->user_file_sizes[i] = is.tellg();
  }

  priv->syst_file_sizes.resize(priv->syst_files.size());
  for (unsigned i = 0; i < priv->syst_files.size(); ++i) {
    ifstream &is = *priv->syst_files[i];
    is.seekg(0, std::ios::end);
    priv->syst_file_sizes[i] = is.tellg();
  }
#else

  priv->user_file_sizes.resize(depctx.user_src_f_paths.size());
  priv->syst_file_sizes.resize(depctx.syst_src_f_paths.size());

  for (unsigned i = 0; i < depctx.user_src_f_paths.size(); ++i) {
    ifstream is(depctx.user_src_f_paths[i]);
    is.seekg(0, std::ios::end);
    priv->user_file_sizes[i] = static_cast<unsigned>(is.tellg());
  }

  for (unsigned i = 0; i < depctx.syst_src_f_paths.size(); ++i) {
    ifstream is(depctx.syst_src_f_paths[i]);
    is.seekg(0, std::ios::end);
    priv->syst_file_sizes[i] = static_cast<unsigned>(is.tellg());
  }
#endif
}

code_reader::~code_reader() {}

string code_reader::source_text(code_t c) {
  const source_range_t &src_rng = priv->g[c];

  if (src_rng.beg == location_dummy_beg && src_rng.end == location_dummy_end)
    // dummy vertex
    return "";

  if (src_rng.beg == location_entire_file_beg &&
      src_rng.end == location_entire_file_end)
    // entire file
    return "";

  auto &sizes = is_system_source_file(src_rng.f) ? priv->syst_file_sizes
                                                 : priv->user_file_sizes;
  auto &paths = is_system_source_file(src_rng.f)
                    ? priv->g[boost::graph_bundle].syst_src_f_paths
                    : priv->g[boost::graph_bundle].user_src_f_paths;

#if 0
  auto &files =
      is_system_source_file(src_rng.f) ? priv->syst_files : priv->user_files;

  ifstream &is = *(files[index_of_source_file(src_rng.f)]);
#else
  ifstream is(paths.at(index_of_source_file(src_rng.f)));
#endif
  if (!is) {
    cerr << "error: could not read "
         << paths.at(index_of_source_file(src_rng.f)) << endl;
    print_istream_error(is);
    exit(1);
  }

  string res;
  res.resize(static_cast<unsigned>(src_rng.end - src_rng.beg));
  is.seekg(static_cast<unsigned>(src_rng.beg) % sizes.at(index_of_source_file(src_rng.f)));
  is.read(&res[0], static_cast<ssize_t>(res.size()));

  if (!is) {
    cerr << "error printing code: while trying to read " << res.size()
         << " bytes only " << is.gcount() << " could be read from "
         << debug_source_description(c) << endl;
    print_istream_error(is);
    exit(1);
  }

#if 0
  res += ("\n/* " + debug_source_description(c) + " */");
#endif
  return res;
}

string code_reader::debug_source_description(code_t c) {
  const source_range_t &src_rng = priv->g[c];

  auto &paths = is_system_source_file(src_rng.f)
                    ? priv->g[boost::graph_bundle].syst_src_f_paths
                    : priv->g[boost::graph_bundle].user_src_f_paths;
  auto &sizes = is_system_source_file(src_rng.f) ? priv->syst_file_sizes
                                                 : priv->user_file_sizes;

  unsigned n = static_cast<unsigned>(src_rng.end - src_rng.beg);

  unsigned beg = static_cast<unsigned>(src_rng.beg) % sizes.at(index_of_source_file(src_rng.f));
  unsigned end = beg + n;

  ostringstream buff;
  buff << (is_system_source_file(src_rng.f) ? " * " : "")
       << paths.at(index_of_source_file(src_rng.f)) << " [" << src_rng.beg
       << ", " << src_rng.end << ") [" << beg << ", " << end << ')';

  return buff.str();
}

string code_reader::source_description(code_t c) {
  const source_range_t &src_rng = priv->g[c];

  auto &paths = is_system_source_file(src_rng.f)
                    ? priv->g[boost::graph_bundle].syst_src_f_paths
                    : priv->g[boost::graph_bundle].user_src_f_paths;
  auto &sizes = is_system_source_file(src_rng.f) ? priv->syst_file_sizes
                                                 : priv->user_file_sizes;

  unsigned n = static_cast<unsigned>(src_rng.end - src_rng.beg);

  unsigned beg = static_cast<unsigned>(src_rng.beg) % sizes.at(index_of_source_file(src_rng.f));
  unsigned end = beg + n;

  ostringstream buff;
  buff << paths.at(index_of_source_file(src_rng.f)) << " [" << beg << ", "
       << end << ')';

  return buff.str();
}
}
