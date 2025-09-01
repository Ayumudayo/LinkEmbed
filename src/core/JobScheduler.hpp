#pragma once
#include <dpp/dpp.h>
#include <functional>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include "../utils/ThreadPool.hpp"

namespace LinkEmbed {
    class JobScheduler {
    public:
        using Job = std::function<void()>;

        JobScheduler(ThreadPool& pool);
        ~JobScheduler();

        void Schedule(dpp::snowflake message_id, int delay_seconds, Job job);
        void Cancel(dpp::snowflake message_id);

    private:
        void Run();

        struct ScheduledJob {
            dpp::snowflake id;
            std::chrono::steady_clock::time_point execution_time;
            Job job;
            bool cancelled = false;
        };

        ThreadPool& thread_pool;
        std::vector<ScheduledJob> jobs;
        std::mutex jobs_mutex;
        std::condition_variable cv;
        std::thread scheduler_thread;
        bool stop_ = false;
    };
}
