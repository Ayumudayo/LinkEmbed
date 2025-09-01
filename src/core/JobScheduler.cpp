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
            // 가장 이른 실행 시간을 가진 작업을 선택하기 위해 정렬
            std::sort(jobs.begin(), jobs.end(), [](const ScheduledJob& a, const ScheduledJob& b) {
                return a.execution_time > b.execution_time; // back()가 가장 이른 작업
            });

            auto now = std::chrono::steady_clock::now();
            ScheduledJob& next_job = jobs.back();

            if (next_job.execution_time <= now) {
                ScheduledJob job_to_run = std::move(next_job);
                jobs.pop_back();
                // 다른 스레드가 Cancel/Schedule 할 수 있도록 잠금 해제 후 실행
                lock.unlock();
                if (!job_to_run.cancelled) {
                    thread_pool.enqueue(job_to_run.job);
                }
                lock.lock();
            } else {
                // 종료 신호를 기다리거나 지정 시각까지 대기
                cv.wait_until(lock, next_job.execution_time, [this]{ return stop_; });
            }
        }
    }
}

void JobScheduler::Schedule(dpp::snowflake message_id, int delay_seconds, Job job) {
    {
        std::unique_lock<std::mutex> lock(jobs_mutex);
        jobs.push_back({
            message_id,
            std::chrono::steady_clock::now() + std::chrono::seconds(delay_seconds),
            std::move(job),
            false
        });
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
        Logger::Log(LogLevel::INFO, "Cancelled job(s) for message ID: " + std::to_string(message_id));
    }
    cv.notify_all();
}

}
