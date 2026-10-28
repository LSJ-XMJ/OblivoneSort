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
        counts_.assign(chunks_, std::vector<std::uint32_t>(buckets_, 0));
        offsets_.assign(chunks_, std::vector<std::uint32_t>(buckets_, 0));
        begin_.assign(buckets_, 0);
        end_.assign(buckets_, 0);
        buffer_.resize(n_);
        cached_ = sort_opt.cache_bucket_index && buckets_ <= 65536;
        if (cached_) cache_.resize(n_);
        scale_ = static_cast<double>(buckets_) /
                 static_cast<double>(maximum_ - minimum_);
    }

    void start() {
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

        auto self = this->shared_from_this();
        plan p;
        while (alloc->next(p)) {
            eng_.submit(
                [self, alloc, p = std::move(p)] {
                    for (const span& s : p.spans) {
                        const std::size_t len = s.last - s.first;
                        if (len < 2) continue;

                        if (len > self->stop_len_ &&
                            p.level < self->sort_opt_.max_depth) {
                            std::uint64_t mn = self->key_(self->data_[s.first]);
                            std::uint64_t mx = mn;
                            for (std::size_t i = s.first + 1; i < s.last; ++i) {
                                const std::uint64_t k =
                                    self->key_(self->data_[i]);
                                if (k < mn) mn = k;
                                if (k > mx) mx = k;
                            }
                            if (mn != mx) {
                                std::size_t child_buckets =
                                    self->sort_opt_.bucket_limit
                                        ? self->sort_opt_.bucket_limit
                                        : 1;
                                if (self->par_opt_.max_buckets) {
                                    child_buckets = (std::min)(
                                        child_buckets,
                                        self->par_opt_.max_buckets);
                                }
                                child_buckets = (std::max)(
                                    std::size_t(1),
                                    (std::min)(len, child_buckets));
                                const std::size_t child_team =
                                    self->eng_.team_for_child();
                                auto child = std::make_shared<root_node>(
                                    self->eng_, self->data_ + s.first, len,
                                    child_team, child_buckets, p.level,
                                    self->stop_len_, mn, mx, self->key_,
                                    self->less_, self->fallback_,
                                    self->sort_opt_, self->par_opt_);
                                child->start();
                                continue;
                            }
                        }

                        std::vector<T> scratch(len);
                        oblivone::detail::sort_range(
                            self->data_ + s.first, len, p.level, self->key_,
                            self->less_, self->fallback_, self->sort_opt_,
                            scratch);
                    }
                },
                2);
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
    double scale_ = 1.0;
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
