#ifndef OBLIVONE_ADVANCED_LEVEL_HPP
#define OBLIVONE_ADVANCED_LEVEL_HPP

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace oblivone {
namespace advanced {

struct span {
    std::size_t first = 0;
    std::size_t last = 0;
};

struct plan {
    std::vector<span> spans;
    int level = 0;
    double target = 0.0;
    double floor = 0.0;
};

// Allocation state for one recursive partition level.
//
// worker_count and slot_count are deliberately separate. worker_count controls
// the 60%/120% rules; slot_count is the overdecomposed task budget.
class allocator {
public:
    allocator(std::vector<span> groups, std::size_t total,
              std::size_t workers, std::size_t slots, int level)
        : groups_(std::move(groups)),
          total_(total),
          workers_(workers ? workers : 1),
          slots_(slots ? slots : workers_),
          left_(total),
          budget_(slots_),
          level_(level) {
        if (slots_ < workers_) slots_ = workers_;
        if (budget_ < workers_) budget_ = workers_;
        update_scale();
    }

    void donate(std::size_t workers, std::size_t slots_per_worker = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workers == 0) return;
        workers_ += workers;
        const std::size_t add =
            workers * (slots_per_worker ? slots_per_worker : 1);
        slots_ += add;
        budget_ += add;
        update_scale();
    }

    // Idempotent: concurrent donors may observe the same idle count.
    void raise(std::size_t target_workers, std::size_t slots_per_worker = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (target_workers <= workers_) return;
        const std::size_t delta = target_workers - workers_;
        workers_ = target_workers;
        const std::size_t add =
            delta * (slots_per_worker ? slots_per_worker : 1);
        slots_ += add;
        budget_ += add;
        update_scale();
    }

    std::size_t workers() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return workers_;
    }

    bool next(plan& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (;;) {
            const double target = target_now();
            if (pending_.size() > 0 &&
                (pending_size_ >= target || cursor_ >= groups_.size())) {
                emit(out);
                return true;
            }
            if (cursor_ >= groups_.size()) {
                if (pending_.size() > 0) {
                    emit(out);
                    return true;
                }
                return false;
            }

            const span s = groups_[cursor_];
            const std::size_t len = s.last - s.first;
            if (pending_size_ + len > 1.6 * target &&
                pending_size_ + len > 1.2 * mean_) {
                out.spans.clear();
                out.spans.push_back(s);
                out.level = level_;
                out.target = target;
                out.floor = floor_;
                ++cursor_;
                left_ -= len;
                if (budget_ > 0) --budget_;
                return true;
            }

            pending_.push_back(s);
            pending_size_ += len;
            ++cursor_;
            if (pending_size_ >= target_now()) {
                emit(out);
                return true;
            }
        }
    }

private:
    void update_scale() {
        mean_ = static_cast<double>(total_) / static_cast<double>(workers_);
        floor_ = 0.6 * mean_;
    }

    double target_now() const {
        const std::size_t budget = budget_ ? budget_ : 1;
        double target =
            (std::max)(0.8 * static_cast<double>(left_) /
                           static_cast<double>(budget),
                       floor_);
        if (budget_ <= 1) target = static_cast<double>(left_);
        return target;
    }

    void emit(plan& out) {
        out.spans = pending_;
        out.level = level_;
        out.target = target_now();
        out.floor = floor_;
        left_ -= pending_size_;
        if (budget_ > 0) --budget_;
        pending_.clear();
        pending_size_ = 0;
    }

    std::vector<span> groups_;
    std::size_t total_ = 0;
    std::size_t workers_ = 1;
    std::size_t slots_ = 1;
    std::size_t left_ = 0;
    std::size_t budget_ = 1;
    int level_ = 0;
    double mean_ = 0.0;
    double floor_ = 0.0;
    std::size_t cursor_ = 0;
    std::vector<span> pending_;
    std::size_t pending_size_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace advanced
}  // namespace oblivone

#endif  // OBLIVONE_ADVANCED_LEVEL_HPP
