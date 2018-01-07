#pragma once
#include "collection.h"
#include <unordered_set>
#include <string>
#include <boost/filesystem.hpp>
#include <vector>
#include <functional>

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

collection_t *link(const collection_sources_t &);
}
