#include "collection.h"
#include "link.h"
#include "toposort.h"
#include "reachable.h"
#include "code_reader.h"
#include "graphviz.h"
#include "static.h"
#include <tuple>
#include <iostream>
#include <sstream>
#include <vector>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>

using namespace std;
using namespace carbon;
namespace po = boost::program_options;
namespace fs = boost::filesystem;
typedef boost::format fmt;

static tuple<fs::path, collection_sources_t, code_location_list_t,
             global_symbol_list_t, int, bool, bool, bool, bool>
parse_command_line_arguments(int argc, char **argv);

int main(int argc, char **argv) {
  fs::path ofp;
  collection_sources_t clc_files;
  code_location_list_t desired_code_locs;
  global_symbol_list_t desired_glbs;
  int verb;
  bool only_tys;
  bool graphviz;
  bool syst_code;
  bool debug;

  //
  // parse command line
  //
  tie(ofp, clc_files, desired_code_locs, desired_glbs, verb, only_tys, graphviz,
      syst_code, debug) = parse_command_line_arguments(argc, argv);

  //
  // take every collection for each source file, and merge (link) them together
  //
  collection_t* clc = link(clc_files);
  code_reader c_reader(*clc);

  //
  // compute a minimal set which contains the requested code
  //
  unordered_set<code_t> reachable;
  set<code_t> desired_code =
      reachable_code(reachable, *clc, desired_code_locs, desired_glbs, only_tys);

  //
  // output graph visualization if requested
  //
  if (graphviz) {
    ofstream dot_f(
        (fmt("%s_deps_reachable_user.dot") % ofp.filename().string()).str(),
        ofstream::out);
    output_graphviz_of_reachable_user_code(dot_f, c_reader, reachable, *clc);
  }

  if (graphviz) {
    ofstream dot_f(
        (fmt("%s_deps_reachable.dot") % ofp.filename().string()).str(),
        ofstream::out);
    output_graphviz_of_reachable_code(dot_f, c_reader, reachable, *clc);
  }

  //
  // topologically sort the code
  //
  list<code_t> toposorted;
  topologically_sort_code(toposorted, *clc);

  //
  //
  //
  unordered_map<code_t, bool*> static_defs_printed;
  build_static_function_definitions_map(*clc, static_defs_printed);

  //
  // print code
  //
  ofstream o(ofp.string(), ofstream::out | ofstream::app);

  //
  // XXX always include stddef.h. unfortunately, stddef.h can be included in a
  // variety of insane ways. e.g., one may #define __need_ptrdiff_t and then
  // later #include <stdlib.h> which #include's <stddef.h>. Thus one may think
  // that since this chain of includes led to the definition of the type
  // ptrdiff_t, printing #include <stdlib.h> will define ptrdiff_t. But this is
  // not so. stdlib.h only #define's __need_size_t on my system, so on its own,
  // such an inclusion will not yield ptrdiff_t.
  //
#if 0
  o << "#include <stddef.h>" << endl << endl;
#endif

  unordered_set<string> sys_hdrs_incl;
  for (code_t c : toposorted) {
    if (is_dummy_code(*clc, c) || reachable.find(c) == reachable.end()) {
      continue;
    } else {
      // check if the code is a definition with the same symbol as a previous
      // definition we've already printed
      auto static_def_it = static_defs_printed.find(c);
      if (static_def_it != static_defs_printed.end()) {
        if (*(*static_def_it).second)
          continue;
        else
          *(*static_def_it).second = true;
      }
    }

    if (!is_system_code(*clc, c) || syst_code) {
      if (debug)
        o << "/* " << c_reader.debug_source_description(c) << " */"
          << endl;

      if (desired_code.find(c) != desired_code.end())
        cout << static_cast<long>(o.tellp()) + 1 << endl;

      o << c_reader.source_text(c) << endl << endl;
      continue;
    }

    if (boost::ends_with(system_header_of_code(*clc, c), "stddefs.h"))
      continue;

    string sys_hdr = top_level_system_header_of_code(*clc, c);

    if (sys_hdrs_incl.find(sys_hdr) != sys_hdrs_incl.end()) {
#if 0
      cerr << "system header '" << sys_hdr << "' already included" << endl;
      cerr << "for " << c_reader.source_text(c) << endl;
#endif
      continue;
    }
    sys_hdrs_incl.insert(sys_hdr);

#if 0
    cerr << "including '" << sys_hdr << '\'' << endl;
    cerr << "for " << c_reader.source_text(c) << endl;
#endif

    const char *inc_str;

    // XXX TODO implement more generally using the -I's passed to clang
    if (boost::starts_with(sys_hdr, "/usr/include/i386-linux-gnu/")) {
      // needed this case for "#include <sys/time.h>"
      inc_str = sys_hdr.c_str() + 13 + 15;
    } else if (boost::starts_with(sys_hdr, "/usr/include/")) {
      inc_str = sys_hdr.c_str() + 13;
    } else {
      vector<char> tmp(sys_hdr.begin(), sys_hdr.end());
      tmp.push_back('\0');
      inc_str = basename(tmp.data());
    }

    o << "#include <" << inc_str << ">" << endl << endl;
  }

  return 0;
}

static int line_number_to_offset(const fs::path& p, int lnno) {
  ifstream f(p.string());

  map<int, int> lnoffmap;

  string ln;
  int off = 0;
  int _lnno = 1;

  lnoffmap[1] = 0;
  while (getline(f, ln)) {
    off += ln.size() + 1;
    ++_lnno;

    lnoffmap[_lnno] = off;
  }

  auto it = lnoffmap.find(lnno);
  if (it == lnoffmap.end()) {
    cerr << "error: given line number " << lnno << " does not exist in " << p
         << endl;
    exit(1);
  }

  return (*it).second;
}

tuple<fs::path, collection_sources_t, code_location_list_t,
      global_symbol_list_t, int, bool, bool, bool, bool>
parse_command_line_arguments(int argc, char **argv) {
  fs::path root_src_dir;
  fs::path root_bin_dir;
  vector<string> code_args;
  vector<string> from_args;
  bool from_all;

  fs::path ofp;
  collection_sources_t cfl;
  code_location_list_t cll;
  global_symbol_list_t gsl;
  int verb;
  bool only_tys;
  bool graphviz;
  bool syst_code;
  bool debug;

  try {
    po::options_description desc("Allowed options");
    desc.add_options()
      ("help,h", "produce help message")

      ("out,o", po::value<fs::path>(&ofp),
       "specify output file path")

      ("src", po::value<fs::path>(&root_src_dir)->default_value(fs::current_path()),
       "specify root source directory where code exists")

      ("bin", po::value<fs::path>(&root_bin_dir)->default_value(fs::current_path()),
       "specify root build directory where carbon files exist")

      ("verbose,v", po::value<int>(&verb)->default_value(0),
       "enable verbosity (optionally specify level)")

      ("code,c", po::value< vector<string> >(&code_args),
       "specify source code to extract. the format of this argument "
       "is:\n[relative source file path]:[line number]l\n[relative "
       "source file path]:[byte offset]o\n[global symbol]")

      ("from,f", po::value< vector<string> >(&from_args),
       "specify an additional source file from which to extract code from (this"
       "option is overrided by --from-all)")

      ("debug", "extract code with comments from whence it came")

      ("from-all,a", "extract code from all known source files")

      ("only-types,t", "only extract types")

      ("graphviz,g", "output graphviz file")

      ("sys-code,s", "inline code from system header files")
    ;

    po::positional_options_description p;
    p.add("code", -1);

    po::variables_map vm;
    po::store(
        po::command_line_parser(argc, argv).options(desc).positional(p).run(),
        vm);
    po::notify(vm);

    if (vm.count("help") || !vm.count("code")) {
      cout << "Usage: carbon-extract [options] code...\n";
      cout << desc;
      exit(0);
    }

    only_tys = vm.count("only-types") != 0;
    graphviz = vm.count("graphviz") != 0;
    syst_code = vm.count("sys-code") != 0;
    from_all = vm.count("from-all") != 0;
    debug = vm.count("debug") != 0;
  } catch (exception &e) {
    cerr << e.what() << endl;
    exit(1);
  }

  root_src_dir = fs::canonical(root_src_dir);

  fs::path carbon_dir(root_bin_dir / ".carbon");
  if (!fs::is_directory(carbon_dir)) {
    cerr << "carbon data not found in " << root_src_dir << endl;
    exit(1);
  }

  cfl.first = carbon_dir;

  if (from_all) {
    fs::recursive_directory_iterator end_iter;
    for (fs::recursive_directory_iterator dir_itr(carbon_dir);
         dir_itr != end_iter; ++dir_itr) {
      if (!fs::is_regular_file(dir_itr->status()))
        continue;

      cfl.second.insert(fs::canonical(dir_itr->path()));
    }
  } else {
    for (const string &relpath : from_args) {
      fs::path abspath1(root_src_dir / relpath);
      if (!fs::is_regular_file(abspath1)) {
        cerr << "source file '" << relpath << "' does not exist" << endl;
        exit(1);
      }

      fs::path abspath2(carbon_dir / (relpath + ".carbon"));
      if (!fs::is_regular_file(abspath2)) {
        cerr << "no carbon collect data for '" << relpath << "'" << endl;
        exit(1);
      }

      cfl.second.insert(fs::canonical(abspath2));
    }
  }

  for (const string& s : code_args) {
    string::size_type colpos = s.find(':');

    if (colpos == string::npos) {
      gsl.push_back(s); // treat as global symbol
      continue;
    }

    if (colpos >= s.size() - 2 ||
        (s[s.size() - 1] != 'l' && s[s.size() - 1] != 'o')) {
      cerr << "invalid specification of code '" << s << "' to extract" << endl;
      exit(1);
    }

    string relpath = s.substr(0, colpos);

    fs::path abspath1 = fs::canonical(root_src_dir / relpath);
    if (!fs::is_regular_file(abspath1)) {
      cerr << "code source file '" << relpath << "' does not exist" << endl;
      exit(1);
    }

    fs::path abspath2(carbon_dir / (relpath + ".carbon"));
    if (!fs::is_regular_file(abspath2)) {
      cerr << "no carbon collect data for '" << relpath << "'" << endl;
      exit(1);
    }
    cfl.second.insert(fs::canonical(abspath2));

    string rest = s.substr(colpos + 1, s.size() - (colpos + 1) - 1);
    int off;
    if (s[s.size()-1] == 'l') {
      off = line_number_to_offset(abspath1, stoi(rest));
    } else {
      off = stoi(rest);
    }

    cll.push_back(make_pair(abspath1.string(), off));
  }

  return make_tuple(ofp, cfl, cll, gsl, verb, only_tys, graphviz, syst_code,
                    debug);
}
