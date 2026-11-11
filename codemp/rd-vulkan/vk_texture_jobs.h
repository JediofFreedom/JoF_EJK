#ifndef VK_TEXTURE_JOBS_H
#define VK_TEXTURE_JOBS_H
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Main thread bounds memory and commits results in registration order. Workers
// only execute memory-only transforms; shutdown joins before their inputs die.
class TextureWorkers {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    std::vector<std::thread> threads;
    bool stopping = false;
public:
    ~TextureWorkers() { shutdown(); }
    bool start(unsigned count) {
        if (!threads.empty()) return true;
        stopping = false;
        try {
            for (unsigned i = 0; i < count; ++i) threads.emplace_back([this]() {
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        wake.wait(lock, [this]() { return stopping || !jobs.empty(); });
                        if (stopping) return;
                        job = std::move(jobs.front()); jobs.pop_front();
                    }
                    job();
                }
            });
        } catch (...) { shutdown(); return false; }
        return !threads.empty();
    }
    void submit(std::function<void()> job) {
        { std::lock_guard<std::mutex> lock(mutex); jobs.push_back(std::move(job)); }
        wake.notify_one();
    }
    void shutdown() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; jobs.clear(); }
        wake.notify_all();
        for (auto &thread : threads) thread.join();
        threads.clear();
    }
};
#endif
