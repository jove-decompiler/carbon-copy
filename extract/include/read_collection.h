#pragma once
#include <string>
#include "collection.h"
#include <boost/filesystem.hpp>

namespace carbon {

void read_collection_file(collection_t &out, const boost::filesystem::path &);
}
