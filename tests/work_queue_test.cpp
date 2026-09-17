#include <source2root/work_queue.hpp>

#include <chrono>
#include <future>
#include <iostream>

static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename Predicate>
static void Await(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done()) {
        Check(std::chrono::steady_clock::now() < deadline, "worker deadline");
        std::this_thread::yield();
    }
}

int main() {
    try {
        const auto owner = std::this_thread::get_id();
        source2root::WorkQueue queue(1, 2);
        std::promise<void> started, release;
        auto gate = release.get_future().share();
        std::atomic_bool worker_ok = false;
        int completions = 0;
        bool paused = true;
        auto first = queue.Submit([&](const auto& canceled) {
            worker_ok = std::this_thread::get_id() != owner && !canceled.load();
            started.set_value();
            gate.wait();
        }, [&](auto error) {
            Check(!error && std::this_thread::get_id() == owner, "completion belongs to server thread");
            ++completions;
            return !paused;
        });
        Check(started.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready, "worker started");
        // Always release the gate before assertions that may throw, so failure
        // reports cannot strand the joined worker during stack unwinding.
        bool canceled_work = false, canceled_completion = false;
        auto second = queue.Submit([&](const auto&) { canceled_work = true; },
            [&](auto) { canceled_completion = true; return true; });
        second.reset();
        const bool bounded = !queue.Submit([](const auto&) {}, [](auto) { return true; });
        release.set_value();
        Check(bounded && worker_ok, "running, queued and retained results share a fixed capacity");
        Await([&] { queue.Dispatch(); return completions != 0 && queue.Pending() == 1; });
        const auto before = completions;
        queue.Dispatch();
        Check(completions == before + 1 && !canceled_work && !canceled_completion,
            "paused completion retained once per dispatch; canceled work skipped");
        paused = false;
        queue.Dispatch();
        Check(queue.Pending() == 0, "resumed completion consumes job");
        queue.Dispatch();
        Check(completions == before + 2, "consumed completion is not repeated");

        bool exception_seen = false;
        auto failed = queue.Submit([](const auto&) { throw std::runtime_error("network failed"); }, [&](auto error) {
            try { if (error) std::rethrow_exception(error); }
            catch (const std::runtime_error& e) { exception_seen = std::string(e.what()) == "network failed"; }
            return true;
        });
        Await([&] { queue.Dispatch(); return queue.Pending() == 0; });
        Check(exception_seen, "worker exceptions delivered on server thread");
        bool wrong_thread = false;
        std::thread foreign([&] {
            try { queue.Dispatch(); }
            catch (const std::logic_error&) { wrong_thread = true; }
        });
        foreign.join();
        Check(wrong_thread, "foreign dispatch rejected");

        bool recursive = false;
        auto reentrant = queue.Submit([](const auto&) {}, [&](auto) {
            try { queue.Dispatch(); }
            catch (const std::logic_error&) { recursive = true; }
            throw std::runtime_error("completion failed");
            return true;
        });
        bool caught = false;
        Await([&] {
            try { queue.Dispatch(); }
            catch (const std::runtime_error&) { caught = true; }
            return queue.Pending() == 0;
        });
        Check(caught && recursive, "throwing completion drains safely and rejects recursive dispatch");
        queue.Dispatch();

        std::promise<void> shutdown_started;
        bool worker_left = false, shutdown_completion = false;
        std::unique_ptr<source2root::WorkQueue::Ticket> retained;
        {
            source2root::WorkQueue stopping(1);
            retained = stopping.Submit([&](const auto& canceled) {
                shutdown_started.set_value();
                while (!canceled.load()) std::this_thread::yield();
                worker_left = true;
            }, [&](auto) { shutdown_completion = true; return true; });
            Check(shutdown_started.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                "shutdown worker started");
        }
        retained.reset();
        Check(worker_left && !shutdown_completion, "shutdown cancels, joins, and suppresses late completions");
        std::cout << "Worker bounds, dispatch ownership, retention, cancellation and shutdown passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
