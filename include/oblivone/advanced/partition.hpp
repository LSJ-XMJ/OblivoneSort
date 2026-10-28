#ifndef OBLIVONE_ADVANCED_PARTITION_HPP
#define OBLIVONE_ADVANCED_PARTITION_HPP

#include <oblivone/sort.hpp>

#include "engine.hpp"
#include "level.hpp"
#include "options.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace oblivone {
namespace advanced {

// One cooperative root distribution. Count and scatter are split into tasks;
// the last completed chunk submits the next phase.
template <class T, class KeyFn, class LessFn, class Fallback>
class root_node : public std::enable_shared_from_this<
                      root_node<T, KeyFn, LessFn, Fallback>> {
public:
    root_node(engine& eng, T* data, std::size_t n, std::size_t workers,
              std::size_t buckets, int depth, std::size_t stop_len,
              std::uint64_t minimum, std::uint64_t maximum,
              const KeyFn& key, const LessFn& less, Fallback& fallback,
              const oblivone::options& sort_opt, const options& par_opt)
        : eng_(eng),
          data_(data),
          n_(n),
          workers_(workers ? workers : 1),
          buckets_(buckets ? buckets : 1),
          depth_(depth),
          stop_len_(stop_len),
          slots_per_worker_(par_opt.task_slots_per_worker
                                ? par_opt.task_slots_per_worker
                                : 1),
          minimum_(minimum),
          maximum_(maximum),
          key_(key),
          less_(less),
          fallback_(fallback),
          sort_opt_(sort_opt),
          par_opt_(par_opt) {
        chunks_ = (std::min)(workers_, n_);
        if (chunks_ == 0) chunks_ = 1;
        cached_ = sort_opt.cache_bucket_index && buckets_ <= 65536;

        if (par_opt.sample_buckets && n_ > 4096 && buckets_ > 1) {
            const std::size_t sample_size = (std::min)(n_, std::size_t(16384));
            buckets_ = (std::min)(buckets_, sample_size);
            if (buckets_ > 1) {
                std::vector<std::uint64_t> sample(sample_size);
                for (std::size_t i = 0; i < sample_size; ++i) {
                    sample[i] = key_(data_[n_ * i / sample_size]);
                }
                std::sort(sample.begin(), sample.end());
                boundaries_.resize(buckets_ - 1);
                for (std::size_t b = 1; b < buckets_; ++b) {
                    boundaries_[b - 1] = sample[b * sample_size / buckets_];
                }
            }
        }
        cached_ = sort_opt.cache_bucket_index && buckets_ <= 65536;

        const std::size_t table_bytes =
            chunks_ * buckets_ * sizeof(std::uint32_t) * 2;
        const std::size_t vector_bytes =
            buckets_ * sizeof(std::uint32_t) * 2;
        const std::size_t buffer_bytes = n_ * sizeof(T);
        const std::size_t cache_bytes = cached_ ? n_ * sizeof(std::uint16_t) : 0;
        const std::size_t boundary_bytes =
            boundaries_.size() * sizeof(std::uint64_t);
        bytes_ = table_bytes + vector_bytes + buffer_bytes + cache_bytes +
                 boundary_bytes;
        bytes_ += bytes_ / 16 + 4096;
        admitted_ = eng_.try_node(bytes_);
        if (!admitted_) {
            bytes_ = 0;
            return;
        }

        try {
            counts_.assign(chunks_, std::vector<std::uint32_t>(buckets_, 0));
            offsets_.assign(chunks_, std::vector<std::uint32_t>(buckets_, 0));
            begin_.assign(buckets_, 0);
            end_.assign(buckets_, 0);
            buffer_.resize(n_);
            if (cached_) cache_.resize(n_);
        } catch (...) {
            eng_.release_node(bytes_);
            admitted_ = false;
            bytes_ = 0;
            throw;
        }
        if (boundaries_.empty()) {
            scale_ = static_cast<double>(buckets_) /
                     static_cast<double>(maximum_ - minimum_);
        }
    }

    bool admitted() const { return admitted_; }

    ~root_node() {
        if (admitted_) eng_.release_node(bytes_);
    }

    void start() {
        if (!admitted_) return;
        remaining_.store(chunks_);
        auto self = this->shared_from_this();
        for (std::size_t i = 0; i < chunks_; ++i) {
            eng_.submit([self, i] { self->count_chunk(i); }, 0,
                        static_cast<int>(i % eng_.size()));
        }
    }

private:
    std::size_t bucket(std::uint64_t k) const {
        if (k <= minimum_) return 0;
        if (k >= maximum_) return buckets_ - 1;
        if (!boundaries_.empty()) {
            const auto it =
                std::upper_bound(boundaries_.begin(), boundaries_.end(), k);
            const std::size_t b =
                static_cast<std::size_t>(it - boundaries_.begin());
            return b >= buckets_ ? buckets_ - 1 : b;
        }
        const std::size_t b =
            static_cast<std::size_t>(static_cast<double>(k - minimum_) * scale_);
        return b >= buckets_ ? buckets_ - 1 : b;
    }

    void count_chunk(std::size_t index) {
        const std::size_t lo = n_ * index / chunks_;
        const std::size_t hi = n_ * (index + 1) / chunks_;
        auto& row = counts_[index];
        if (cached_) {
            for (std::size_t i = lo; i < hi; ++i) {
                const std::size_t b = bucket(key_(data_[i]));
                cache_[i] = static_cast<std::uint16_t>(b);
                ++row[b];
            }
        } else {
            for (std::size_t i = lo; i < hi; ++i) {
                ++row[bucket(key_(data_[i]))];
            }
        }
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            complete_count();
        }
    }

    void complete_count() {
        std::vector<std::uint32_t> total(buckets_, 0);
        for (const auto& row : counts_) {
            for (std::size_t b = 0; b < buckets_; ++b) total[b] += row[b];
        }
        std::uint32_t sum = 0;
        for (std::size_t b = 0; b < buckets_; ++b) {
            begin_[b] = sum;
            sum += total[b];
            end_[b] = sum;
        }
        std::vector<std::uint32_t> cursor = begin_;
        for (std::size_t t = 0; t < chunks_; ++t) {
            for (std::size_t b = 0; b < buckets_; ++b) {
                offsets_[t][b] = cursor[b];
                cursor[b] += counts_[t][b];
            }
        }
        counts_.clear();
        counts_.shrink_to_fit();

        remaining_.store(chunks_);
        auto self = this->shared_from_this();
        for (std::size_t i = 0; i < chunks_; ++i) {
            eng_.submit([self, i] { self->scatter_chunk(i); }, 0,
                        static_cast<int>(i % eng_.size()));
        }
    }

    void scatter_chunk(std::size_t index) {
        const std::size_t lo = n_ * index / chunks_;
        const std::size_t hi = n_ * (index + 1) / chunks_;
        auto& off = offsets_[index];
        if (cached_) {
            for (std::size_t i = lo; i < hi; ++i) {
                buffer_[off[cache_[i]]++] = data_[i];
            }
        } else {
            for (std::size_t i = lo; i < hi; ++i) {
                buffer_[off[bucket(key_(data_[i]))]++] = data_[i];
            }
        }
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            complete_scatter();
        }
    }

    void complete_scatter() {
        std::copy(buffer_.begin(), buffer_.end(), data_);
        std::vector<span> groups;
        groups.reserve(buckets_);
        for (std::size_t b = 0; b < buckets_; ++b) {
            if (end_[b] > begin_[b]) {
                groups.push_back({static_cast<std::size_t>(begin_[b]),
                                  static_cast<std::size_t>(end_[b])});
            }
        }

        const std::size_t slots =
            (std::max)(workers_, workers_ * slots_per_worker_);
        auto alloc = std::make_shared<allocator>(
            std::move(groups), n_, workers_, slots, depth_ + 1);

        auto pumps = std::make_shared<pump_state>();
        pumps->remaining.store(eng_.size());
        std::weak_ptr<allocator> weak = alloc;
        const std::size_t max_team = eng_.size();
        const std::size_t slot_fanout = slots_per_worker_;
        pumps->observer = eng_.add_observer(
            [weak, max_team, slot_fanout](std::size_t idle) {
                if (auto state = weak.lock()) {
                    const std::size_t target =
                        (std::min)(max_team, idle + 1);
                    state->raise(target, slot_fanout);
                }
            });

        auto self = this->shared_from_this();
        for (unsigned i = 0; i < eng_.size(); ++i) {
            eng_.submit([self, alloc, pumps] { self->pump(alloc, pumps); }, 2,
                        static_cast<int>(i % eng_.size()));
        }
    }

    struct pump_state {
        std::atomic<std::size_t> remaining{0};
        std::size_t observer = 0;
    };

    void pump(const std::shared_ptr<allocator>& alloc,
              const std::shared_ptr<pump_state>& pumps) {
        constexpr int kBatch = 2;
        for (int batch = 0; batch < kBatch; ++batch) {
            const std::size_t target =
                (std::min)(static_cast<std::size_t>(eng_.size()),
                           eng_.idle() + 1);
            alloc->raise(target, slots_per_worker_);

            plan p;
            if (!alloc->next(p)) {
                if (pumps->remaining.fetch_sub(1, std::memory_order_acq_rel) ==
                    1) {
                    eng_.remove_observer(pumps->observer);
                }
                return;
            }
            handle_plan(p);
        }

        auto self = this->shared_from_this();
        eng_.submit([self, alloc, pumps] { self->pump(alloc, pumps); }, 2);
    }

    void handle_plan(const plan& p) {
        for (const span& s : p.spans) {
            const std::size_t len = s.last - s.first;
            if (len < 2) continue;

            if (len > stop_len_ && p.level < sort_opt_.max_depth) {
                std::uint64_t mn = key_(data_[s.first]);
                std::uint64_t mx = mn;
                for (std::size_t i = s.first + 1; i < s.last; ++i) {
                    const std::uint64_t k = key_(data_[i]);
                    if (k < mn) mn = k;
                    if (k > mx) mx = k;
                }
                if (mn != mx) {
                    std::size_t child_buckets = sort_opt_.bucket_limit
                                                    ? sort_opt_.bucket_limit
                                                    : 1;
                    if (par_opt_.max_buckets) {
                        child_buckets =
                            (std::min)(child_buckets, par_opt_.max_buckets);
                    }
                    child_buckets = (std::max)(
                        std::size_t(1), (std::min)(len, child_buckets));
                    const std::size_t child_team = eng_.team_for_child();
                    auto child = std::make_shared<root_node>(
                        eng_, data_ + s.first, len, child_team, child_buckets,
                        p.level, stop_len_, mn, mx, key_, less_, fallback_,
                        sort_opt_, par_opt_);
                    if (child->admitted()) {
                        child->start();
                        continue;
                    }
                }
            }

            std::vector<T> scratch(len);
            oblivone::detail::sort_range(data_ + s.first, len, p.level, key_,
                                         less_, fallback_, sort_opt_, scratch);
        }
    }

    engine& eng_;
    T* data_;
    std::size_t n_;
    std::size_t workers_;
    std::size_t buckets_;
    int depth_ = 0;
    std::size_t stop_len_ = 0;
    std::size_t slots_per_worker_;
    std::uint64_t minimum_;
    std::uint64_t maximum_;
    const KeyFn& key_;
    const LessFn& less_;
    Fallback& fallback_;
    const oblivone::options& sort_opt_;
    const options& par_opt_;
    std::size_t chunks_ = 1;
    bool admitted_ = false;
    std::size_t bytes_ = 0;
    double scale_ = 1.0;
    std::vector<std::uint64_t> boundaries_;
    std::vector<std::vector<std::uint32_t>> counts_;
    std::vector<std::vector<std::uint32_t>> offsets_;
    std::vector<std::uint32_t> begin_;
    std::vector<std::uint32_t> end_;
    std::vector<T> buffer_;
    bool cached_ = false;
    std::vector<std::uint16_t> cache_;
    std::atomic<std::size_t> remaining_{0};
};

}  // namespace advanced
}  // namespace oblivone

#endif  // OBLIVONE_ADVANCED_PARTITION_HPP
