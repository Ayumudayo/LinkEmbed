use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use serenity::all::MessageId;

#[derive(Clone, Default)]
pub struct ReplyTracker {
    inner: Arc<Mutex<HashMap<MessageId, MessageId>>>,
}

impl ReplyTracker {
    pub fn remember_reply(&self, original_message_id: MessageId, reply_message_id: MessageId) {
        if let Ok(mut replies) = self.inner.lock() {
            replies.insert(original_message_id, reply_message_id);
        }
    }

    pub fn take_reply(&self, original_message_id: MessageId) -> Option<MessageId> {
        self.inner
            .lock()
            .ok()
            .and_then(|mut replies| replies.remove(&original_message_id))
    }

    pub fn take_reply_if_matches(
        &self,
        original_message_id: MessageId,
        expected_reply_id: MessageId,
    ) -> Option<MessageId> {
        self.inner
            .lock()
            .ok()
            .and_then(|mut replies| match replies.get(&original_message_id).copied() {
                Some(current_id) if current_id == expected_reply_id => {
                    replies.remove(&original_message_id)
                }
                _ => None,
            })
    }
}

#[cfg(test)]
mod tests {
    use serenity::all::MessageId;

    use super::ReplyTracker;

    #[test]
    fn take_reply_removes_tracked_reply() {
        let tracker = ReplyTracker::default();
        tracker.remember_reply(MessageId::new(1), MessageId::new(2));

        assert_eq!(tracker.take_reply(MessageId::new(1)), Some(MessageId::new(2)));
        assert_eq!(tracker.take_reply(MessageId::new(1)), None);
    }

    #[test]
    fn take_reply_if_matches_only_removes_expected_reply() {
        let tracker = ReplyTracker::default();
        tracker.remember_reply(MessageId::new(1), MessageId::new(2));

        assert_eq!(
            tracker.take_reply_if_matches(MessageId::new(1), MessageId::new(9)),
            None
        );
        assert_eq!(tracker.take_reply(MessageId::new(1)), Some(MessageId::new(2)));
    }
}
