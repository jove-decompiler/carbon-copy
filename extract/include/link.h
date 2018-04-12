#pragma once
#include "collection.h"
#include <boost/filesystem.hpp>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace carbon {

struct boost_filesystem_path_hasher_t {
   size_t operator() (const boost::filesystem::path &p) const {
     return std::hash<std::string>()(p.string());
   }
};

typedef std::pair<
    boost::filesystem::path,
    std::unordered_set<boost::filesystem::path, boost_filesystem_path_hasher_t>>
    collection_sources_t;

void link(depends_t &out, const collection_sources_t &);
}
