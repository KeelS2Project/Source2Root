#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace source2root {

// Workers may only use copied input and private result state, never NativeCall
// or engine/VM objects. Dispatch and Submit belong to the constructing thread.
// Return false from a completion to retain it (for example, a paused script).
// Keep the extension loaded while Pending() != 0, and call Dispatch each frame.
// Destruction cancels outstanding jobs and joins every worker; I/O tasks must
// supply their own finite timeouts and check cooperative cancellation.
class WorkQueue final {
    struct Job {
        std::atomic_bool canceled{false};
        std::function<void(const std::atomic_bool&)> work;
        std::function<bool(std::exception_ptr)> completion;
        std::exception_ptr error;
    };

public:
    class Ticket final {
    public:
        ~Ticket() {
            Cancel();
        }

        void Cancel() noexcept {
            job_->canceled.store(true, std::memory_order_relaxed);
        }

        Ticket(const Ticket&) = delete;
        Ticket& operator=(const Ticket&) = delete;

    private:
        friend class WorkQueue;
        explicit Ticket(std::shared_ptr<Job> job) : job_(std::move(job)) {}

        std::shared_ptr<Job> job_;
    };

    explicit WorkQueue(unsigned workers = 2, unsigned capacity = 64)
        : owner_(std::this_thread::get_id()), capacity_(capacity) {
        if (!workers || workers > 8 || !capacity || capacity > 1024)
            throw std::invalid_argument("Invalid worker queue limits.");

        try {
            jobs_.reserve(capacity);
            ready_.reserve(capacity);

            for (unsigned i = 0; i < workers; ++i)
                workers_.emplace_back([this] {
                    Run();
                });
        } catch (...) {
            Stop();
            throw;
        }
    }

    ~WorkQueue() {
        Stop();
    }

    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    std::unique_ptr<Ticket> Submit(std::function<void(const std::atomic_bool&)> work,
        std::function<bool(std::exception_ptr)> completion) {
        Thread();

        if (!work || !completion)
            throw std::invalid_argument("Missing job function.");

        auto job = std::make_shared<Job>();
        job->work = std::move(work);
        job->completion = std::move(completion);

        auto ticket = std::unique_ptr<Ticket>(new Ticket(job));
        {
            std::lock_guard lock(mutex_);

            if (stopping_)
                throw std::logic_error("Worker queue is stopping.");

            if (jobs_.size() >= capacity_)
                return {};

            jobs_.push_back(job);

            try {
                waiting_.push_back(job);
            } catch (...) {
                jobs_.pop_back();
                throw;
            }
        }

        wake_.notify_one();
        return ticket;
    }

    // A retained completion runs at most once per Dispatch; a callback that
    // submits more work cannot make this frame's drain unbounded.
    void Dispatch() {
        Thread();

        if (dispatching_)
            throw std::logic_error("Worker completions cannot dispatch recursively.");

        struct Scope {
            bool& active;
            ~Scope() {
                active = false;
            }
        } scope{dispatching_};
        dispatching_ = true;
        std::size_t remaining;
        {
            std::lock_guard lock(mutex_);
            remaining = ready_.size();
        }
        while (remaining--) {
            std::shared_ptr<Job> job;
            {
                std::lock_guard lock(mutex_);
                job = ready_.front();
                ready_.erase(ready_.begin());
            }

            bool consumed = job->canceled.load(std::memory_order_relaxed);
            std::exception_ptr failure;

            if (!consumed) {
                try {
                    consumed = job->completion(job->error);
                } catch (...) {
                    consumed = true;
                    failure = std::current_exception();
                }
            }

            if (consumed || job->canceled.load(std::memory_order_relaxed)) {
                // Release closures on the server thread, outside the lock.
                job->work = {};
                job->completion = {};
                job->error = {};
                std::lock_guard lock(mutex_);
                std::erase(jobs_, job);
            } else {
                std::lock_guard lock(mutex_);
                ready_.push_back(std::move(job));
            }

            if (failure)
                std::rethrow_exception(failure);
        }
    }

    std::size_t Pending() const {
        std::lock_guard lock(mutex_);
        return jobs_.size();
    }

private:
    void Thread() const {
        if (owner_ != std::this_thread::get_id())
            throw std::logic_error("Worker queue requires its owner thread.");
    }

    void Run() noexcept {
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] {
                    return stopping_ || !waiting_.empty();
                });

                if (waiting_.empty())
                    return;

                job = waiting_.front();
                waiting_.pop_front();
            }

            if (!job->canceled.load(std::memory_order_relaxed)) {
                try {
                    job->work(job->canceled);
                } catch (...) {
                    job->error = std::current_exception();
                }
            }

            std::lock_guard lock(mutex_);
            ready_.push_back(std::move(job));
        }
    }

    void Stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;

            for (const auto& job : jobs_)
                job->canceled.store(true, std::memory_order_relaxed);
        }

        wake_.notify_all();

        for (auto& worker : workers_)
            if (worker.joinable())
                worker.join();
        // All closures are now destroyed by the owner, after workers have left.
        for (const auto& job : jobs_) {
            job->work = {};
            job->completion = {};
            job->error = {};
        }

        waiting_.clear();
        ready_.clear();
        jobs_.clear();
    }

    const std::thread::id owner_;
    const unsigned capacity_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::thread> workers_;
    std::vector<std::shared_ptr<Job>> jobs_, ready_;
    std::deque<std::shared_ptr<Job>> waiting_;
    bool stopping_ = false, dispatching_ = false;
};

}
