#include "JobScheduler.hpp"
#include "../utils/Logger.hpp"
#include <algorithm>

namespace LinkEmbed {

JobScheduler::JobScheduler(ThreadPool& pool) : thread_pool(pool), stop_(false) {
    scheduler_thread = std::thread(&JobScheduler::Run, this);
}

JobScheduler::~JobScheduler() {
    {
        std::unique_lock<std::mutex> lock(jobs_mutex);
        stop_ = true;
    }
    cv.notify_all();
    if (scheduler_thread.joinable()) {
        scheduler_thread.join();
    }
}

void JobScheduler::Run() {
    std::unique_lock<std::mutex> lock(jobs_mutex);
    while (!stop_) {
        if (jobs.empty()) {
            cv.wait(lock, [this] { return stop_ || !jobs.empty(); });
            if (stop_) break;
        } else {
            // Sort to select the job with the earliest execution time
            std::sort(jobs.begin(), jobs.end(), [](const ScheduledJob& a, const ScheduledJob& b) {
                return a.execution_time > b.execution_time; // back() will be the earliest job
            });

            auto now = std::chrono::steady_clock::now();
            ScheduledJob& next_job = jobs.back();

            if (next_job.execution_time <= now) {
                ScheduledJob job_to_run = std::move(next_job);
                jobs.pop_back();
                // Unlock before executing so other threads can Cancel/Schedule
                lock.unlock();
                if (!job_to_run.cancelled) {
                    thread_pool.enqueue(job_to_run.job);
                }
                lock.lock();
            } else {
                // Wait for the stop signal or until the specified time
                cv.wait_until(lock, next_job.execution_time, [this]{ return stop_; });
            }
        }
    }
}

void JobScheduler::Schedule(dpp::snowflake message_id, int delay_seconds, Job job) {
    {
        std::unique_lock<std::mutex> lock(jobs_mutex);

        // Check if a job with this ID already exists.
        auto it = std::find_if(jobs.begin(), jobs.end(), [message_id](const ScheduledJob& j) {
            return j.id == message_id;
        });

        if (it != jobs.end()) {
            // If it exists, update it.
            it->execution_time = std::chrono::steady_clock::now() + std::chrono::seconds(delay_seconds);
            it->job = std::move(job);
            it->cancelled = false; // In case it was cancelled before
            Logger::Log(LogLevel::Debug, "Updating existing job for message ID: " + std::to_string(message_id));
        } else {
            // If it doesn't exist, add a new one.
            jobs.push_back({
                message_id,
                std::chrono::steady_clock::now() + std::chrono::seconds(delay_seconds),
                std::move(job),
                false
            });
        }
    }
    cv.notify_one();
}

void JobScheduler::Cancel(dpp::snowflake message_id) {
    std::unique_lock<std::mutex> lock(jobs_mutex);
    bool any = false;
    for (auto& job : jobs) {
        if (job.id == message_id) {
            job.cancelled = true;
            any = true;
        }
    }
    if (any) {
        Logger::Log(LogLevel::Info, "Cancelled job(s) for message ID: " + std::to_string(message_id));
    }
    cv.notify_all();
}

}
