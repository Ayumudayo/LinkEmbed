use std::collections::HashMap;
use std::future::Future;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use serenity::all::MessageId;
use tokio::sync::Semaphore;
use tokio::task::JoinHandle;
use tokio::time::sleep;

#[derive(Clone)]
pub struct JobScheduler {
    inner: Arc<JobSchedulerState>,
}

struct JobSchedulerState {
    delay: Duration,
    worker_limiter: Arc<Semaphore>,
    scheduled_jobs: Mutex<HashMap<MessageId, ScheduledJob>>,
    next_job_token: AtomicU64,
}

struct ScheduledJob {
    token: u64,
    handle: JoinHandle<()>,
}

impl JobScheduler {
    pub fn new(delay: Duration, worker_count: usize) -> Self {
        Self {
            inner: Arc::new(JobSchedulerState {
                delay,
                worker_limiter: Arc::new(Semaphore::new(worker_count)),
                scheduled_jobs: Mutex::new(HashMap::new()),
                next_job_token: AtomicU64::new(1),
            }),
        }
    }

    pub fn schedule<F, Fut>(&self, message_id: MessageId, task: F)
    where
        F: FnOnce() -> Fut + Send + 'static,
        Fut: Future<Output = ()> + Send + 'static,
    {
        let token = self.inner.next_job_token.fetch_add(1, Ordering::Relaxed);
        let delay = self.inner.delay;
        let scheduler = self.clone();
        let worker_limiter = self.inner.worker_limiter.clone();

        let handle = tokio::spawn(async move {
            sleep(delay).await;
            if !scheduler.begin_job(message_id, token) {
                return;
            }

            let Ok(_permit) = worker_limiter.acquire_owned().await else {
                return;
            };

            task().await;
        });

        if let Ok(mut scheduled_jobs) = self.inner.scheduled_jobs.lock() {
            if let Some(existing) =
                scheduled_jobs.insert(message_id, ScheduledJob { token, handle })
            {
                existing.handle.abort();
            }
        }
    }

    pub fn cancel(&self, message_id: MessageId) {
        if let Ok(mut scheduled_jobs) = self.inner.scheduled_jobs.lock() {
            if let Some(existing) = scheduled_jobs.remove(&message_id) {
                existing.handle.abort();
            }
        }
    }

    fn begin_job(&self, message_id: MessageId, token: u64) -> bool {
        let Ok(mut scheduled_jobs) = self.inner.scheduled_jobs.lock() else {
            return false;
        };

        if let Some(current) = scheduled_jobs.get(&message_id) {
            if current.token == token {
                scheduled_jobs.remove(&message_id);
                return true;
            }
        }

        false
    }
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;
    use std::time::Duration;

    use serenity::all::MessageId;

    use super::JobScheduler;

    #[tokio::test]
    async fn cancelled_jobs_do_not_run() {
        let scheduler = JobScheduler::new(Duration::from_millis(40), 1);
        let runs = Arc::new(AtomicUsize::new(0));
        let runs_clone = runs.clone();

        scheduler.schedule(MessageId::new(1), move || async move {
            runs_clone.fetch_add(1, Ordering::SeqCst);
        });
        scheduler.cancel(MessageId::new(1));

        tokio::time::sleep(Duration::from_millis(80)).await;
        assert_eq!(runs.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn replacing_a_job_keeps_only_the_latest_one() {
        let scheduler = JobScheduler::new(Duration::from_millis(20), 1);
        let runs = Arc::new(AtomicUsize::new(0));

        let first_runs = runs.clone();
        scheduler.schedule(MessageId::new(7), move || async move {
            first_runs.fetch_add(1, Ordering::SeqCst);
        });

        let second_runs = runs.clone();
        scheduler.schedule(MessageId::new(7), move || async move {
            second_runs.fetch_add(1, Ordering::SeqCst);
        });

        tokio::time::sleep(Duration::from_millis(60)).await;
        assert_eq!(runs.load(Ordering::SeqCst), 1);
    }
}
