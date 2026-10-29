#ifndef OBLIVONE_ADVANCED_OPTIONS_HPP
#define OBLIVONE_ADVANCED_OPTIONS_HPP

#include <cstddef>

namespace oblivone {
namespace advanced {

struct options {
    // 0 = automatic: 4 below 1e7, 12 at/above 1e7, capped by hardware.
    unsigned threads = 0;
    std::size_t min_n = 2000000;
    std::size_t max_buckets = 65536;
    // 0 = automatic. Numeric inputs use a value-linear root partition.
    std::size_t coarse_buckets = 0;
    std::size_t local_bucket_limit = 0;
    // Logical task slots per worker for the initial group plan.
    std::size_t task_slots_per_worker = 2;
    // Workers kept out of inner/group lanes.
    std::size_t reserved = 1;
    // 0 = 2 * threads.
    std::size_t max_nodes = 0;
    // 0 = unlimited; approximate bytes for concurrent partition nodes.
    std::size_t memory_budget = 0;
    // 0 = n * sizeof(T). Limits concurrent group scratch buffers.
    std::size_t scratch_budget = 0;
    // Opt-in sampled quantile bucket boundaries (off by default).
    bool sample_buckets = false;
};

}  // namespace advanced
}  // namespace oblivone

#endif  // OBLIVONE_ADVANCED_OPTIONS_HPP
