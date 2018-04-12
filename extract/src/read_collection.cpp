#include "read_collection.h"
#include <collect_impl.h>
#include <fstream>
#include <boost/graph/adj_list_serialize.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/list.hpp>
#define CARBON_BINARY
#ifdef CARBON_BINARY
#include <boost/archive/binary_iarchive.hpp>
#else
#include <boost/archive/text_iarchive.hpp>
#endif

using namespace std;
namespace fs = boost::filesystem;

namespace carbon {

void read_collection_file(depends_t &g, const fs::path &p) {
  ifstream ifs(p.string());
#ifdef CARBON_BINARY
  boost::archive::binary_iarchive ia(ifs);
#else
  boost::archive::text_iarchive ia(ifs);
#endif
  ia >> g;
}

}
