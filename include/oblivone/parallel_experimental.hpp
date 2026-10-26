// Deprecated compatibility wrapper.
// The final parallel API lives in <oblivone/parallel.hpp>.

#ifndef OBLIVONE_PARALLEL_EXPERIMENTAL_HPP
#define OBLIVONE_PARALLEL_EXPERIMENTAL_HPP

#include "parallel.hpp"

namespace oblivone {
namespace experimental {
using parallel_options = oblivone::parallel_options;
using oblivone::parallel_sort;
using oblivone::parallel_sort_by;
}  // namespace experimental
}  // namespace oblivone

#endif  // OBLIVONE_PARALLEL_EXPERIMENTAL_HPP
