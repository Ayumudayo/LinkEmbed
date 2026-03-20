use std::collections::{HashMap, VecDeque};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use crate::metadata::Metadata;

pub struct MetadataCache {
    inner: Mutex<CacheState>,
}

struct CacheState {
    max_size: usize,
    max_bytes: usize,
    ttl: Duration,
    current_bytes: usize,
    next_generation: u64,
    entries: HashMap<String, CacheEntry>,
    order: VecDeque<(String, u64)>,
}

#[derive(Clone)]
struct CacheEntry {
    metadata: Metadata,
    expires_at: Instant,
    payload_bytes: usize,
    generation: u64,
}

impl MetadataCache {
    pub fn new(max_size: usize, ttl_minutes: u64, max_bytes: usize) -> Self {
        Self {
            inner: Mutex::new(CacheState {
                max_size,
                max_bytes,
                ttl: Duration::from_secs(ttl_minutes.saturating_mul(60)),
                current_bytes: 0,
                next_generation: 1,
                entries: HashMap::new(),
                order: VecDeque::new(),
            }),
        }
    }

    pub fn get(&self, url: &str) -> Option<Metadata> {
        let mut state = self.inner.lock().ok()?;
        let now = Instant::now();

        let expired = state
            .entries
            .get(url)
            .map(|entry| now >= entry.expires_at)
            .unwrap_or(false);

        if expired {
            remove_entry(&mut state, url);
            return None;
        }

        let generation = next_generation(&mut state);
        let metadata = {
            let entry = state.entries.get_mut(url)?;
            entry.generation = generation;
            entry.metadata.clone()
        };
        state.order.push_back((url.to_string(), generation));
        Some(metadata)
    }

    pub fn put(&self, url: impl Into<String>, metadata: Metadata) {
        let url = url.into();
        let mut state = match self.inner.lock() {
            Ok(guard) => guard,
            Err(_) => return,
        };

        if state.entries.contains_key(&url) {
            remove_entry(&mut state, &url);
        }

        let payload_bytes = estimate_entry_size(&url, &metadata);
        if state.max_bytes > 0 && payload_bytes > state.max_bytes {
            return;
        }

        shrink_to_fit(&mut state, payload_bytes);

        let generation = next_generation(&mut state);
        let expires_at = Instant::now() + state.ttl;
        state.entries.insert(
            url.clone(),
            CacheEntry {
                metadata,
                expires_at,
                payload_bytes,
                generation,
            },
        );
        state.current_bytes += payload_bytes;
        state.order.push_back((url, generation));
    }
}

fn estimate_entry_size(url: &str, metadata: &Metadata) -> usize {
    url.len()
        + metadata.title.len()
        + metadata.image_url.len()
        + metadata.description.len()
        + metadata.site_name.len()
}

fn next_generation(state: &mut CacheState) -> u64 {
    let generation = state.next_generation;
    state.next_generation = state.next_generation.wrapping_add(1).max(1);
    generation
}

fn remove_entry(state: &mut CacheState, url: &str) {
    if let Some(entry) = state.entries.remove(url) {
        state.current_bytes = state.current_bytes.saturating_sub(entry.payload_bytes);
    }
}

fn shrink_to_fit(state: &mut CacheState, incoming_bytes: usize) {
    let enforce_entry_limit = state.max_size > 0;
    let enforce_byte_limit = state.max_bytes > 0;

    while !state.entries.is_empty()
        && ((enforce_entry_limit && state.entries.len() >= state.max_size)
            || (enforce_byte_limit && state.current_bytes + incoming_bytes > state.max_bytes))
    {
        let Some((url, generation)) = state.order.pop_front() else {
            break;
        };

        let should_remove = state
            .entries
            .get(&url)
            .map(|entry| entry.generation == generation)
            .unwrap_or(false);

        if should_remove {
            remove_entry(state, &url);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::MetadataCache;
    use crate::metadata::Metadata;

    #[test]
    fn lru_eviction_works() {
        let cache = MetadataCache::new(1, 10, 256);
        cache.put(
            "http://a",
            Metadata {
                title: "A".to_string(),
                ..Default::default()
            },
        );
        cache.put(
            "http://b",
            Metadata {
                title: "B".to_string(),
                ..Default::default()
            },
        );

        assert!(cache.get("http://a").is_none());
        assert_eq!(cache.get("http://b").unwrap().title, "B");
    }

    #[test]
    fn byte_budget_is_enforced() {
        let cache = MetadataCache::new(10, 10, 60);
        let small = Metadata {
            description: "s".repeat(20),
            ..Default::default()
        };

        cache.put("http://s1", small.clone());
        cache.put("http://s2", small);
        assert!(cache.get("http://s1").is_some());
        assert!(cache.get("http://s2").is_some());

        let large = Metadata {
            description: "l".repeat(80),
            ..Default::default()
        };
        cache.put("http://large", large);
        assert!(cache.get("http://large").is_none());

        let medium = Metadata {
            description: "m".repeat(30),
            ..Default::default()
        };
        cache.put("http://s3", medium.clone());
        assert!(cache.get("http://s1").is_none());
        assert!(cache.get("http://s2").is_none());
        assert_eq!(
            cache.get("http://s3").unwrap().description,
            medium.description
        );
    }
}
