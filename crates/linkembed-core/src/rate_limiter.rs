use std::sync::Mutex;
use std::time::Instant;

pub struct RateLimiter {
    rate: f64,
    state: Mutex<State>,
}

struct State {
    allowance: f64,
    last_check: Instant,
}

impl RateLimiter {
    pub fn new(rate: f64) -> Self {
        Self {
            rate,
            state: Mutex::new(State {
                allowance: rate,
                last_check: Instant::now(),
            }),
        }
    }

    pub fn try_acquire(&self) -> bool {
        let mut state = match self.state.lock() {
            Ok(guard) => guard,
            Err(_) => return false,
        };

        let now = Instant::now();
        let elapsed = now.duration_since(state.last_check).as_secs_f64();
        state.last_check = now;

        state.allowance += elapsed * self.rate;
        if state.allowance > self.rate {
            state.allowance = self.rate;
        }

        if state.allowance >= 1.0 {
            state.allowance -= 1.0;
            true
        } else {
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use std::thread;
    use std::time::Duration;

    use super::RateLimiter;

    #[test]
    fn enforces_basic_token_bucket_behavior() {
        let limiter = RateLimiter::new(2.0);
        assert!(limiter.try_acquire());
        assert!(limiter.try_acquire());
        assert!(!limiter.try_acquire());
        thread::sleep(Duration::from_millis(600));
        assert!(limiter.try_acquire());
    }
}
