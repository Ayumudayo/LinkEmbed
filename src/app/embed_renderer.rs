use linkembed_core::Preview;
use serenity::all::{
    ChannelId, CreateAllowedMentions, CreateEmbed, CreateEmbedAuthor, CreateMessage, MessageId,
    MessageReference,
};

#[derive(Clone, Default)]
pub struct EmbedRenderer;

#[derive(Debug, PartialEq, Eq)]
struct EmbedSpec {
    author_name: Option<String>,
    title: Option<String>,
    url: Option<String>,
    description: Option<String>,
    thumbnail: Option<String>,
}

impl EmbedRenderer {
    pub fn build_reply(
        &self,
        channel_id: ChannelId,
        message_id: MessageId,
        url: &str,
        preview: Preview,
    ) -> CreateMessage {
        let spec = self.spec_for(url, &preview);
        let mut embed = CreateEmbed::new();

        if let Some(author_name) = spec.author_name {
            embed = embed.author(CreateEmbedAuthor::new(author_name).url(url.to_string()));
        }
        if let Some(title) = spec.title {
            embed = embed.title(title);
        }
        if let Some(embed_url) = spec.url {
            embed = embed.url(embed_url);
        }
        if let Some(description) = spec.description {
            embed = embed.description(description);
        }
        if let Some(thumbnail) = spec.thumbnail {
            embed = embed.thumbnail(thumbnail);
        }

        CreateMessage::new()
            .embed(embed)
            .reference_message(MessageReference::from((channel_id, message_id)))
            .allowed_mentions(CreateAllowedMentions::new().replied_user(false))
    }

    fn spec_for(&self, url: &str, preview: &Preview) -> EmbedSpec {
        let author_name = if preview.site_name.is_empty() {
            fallback_site_name(url)
        } else {
            Some(preview.site_name.clone())
        };

        EmbedSpec {
            author_name,
            title: (!preview.title.is_empty()).then(|| preview.title.clone()),
            url: (!url.is_empty()).then(|| url.to_string()),
            description: (!preview.description.is_empty()).then(|| preview.description.clone()),
            thumbnail: (!preview.image_url.is_empty()).then(|| preview.image_url.clone()),
        }
    }
}

fn fallback_site_name(url: &str) -> Option<String> {
    let scheme_pos = url.find("://")?;
    let host_start = scheme_pos + 3;
    let host_end = url[host_start..]
        .find(['/', '\\', '?', '#'])
        .map(|offset| host_start + offset)
        .unwrap_or(url.len());

    let host = url[host_start..host_end].to_ascii_lowercase();
    if host.is_empty() {
        None
    } else {
        Some(host.strip_prefix("www.").unwrap_or(&host).to_string())
    }
}

#[cfg(test)]
mod tests {
    use linkembed_core::Preview;

    use super::{EmbedRenderer, EmbedSpec};

    #[test]
    fn omits_empty_fields_and_uses_host_fallback() {
        let renderer = EmbedRenderer;
        let preview = Preview {
            title: "Example title".to_string(),
            ..Preview::default()
        };

        let spec = renderer.spec_for("https://www.example.com/path", &preview);
        assert_eq!(
            spec,
            EmbedSpec {
                author_name: Some("example.com".to_string()),
                title: Some("Example title".to_string()),
                url: Some("https://www.example.com/path".to_string()),
                description: None,
                thumbnail: None,
            }
        );
    }

    #[test]
    fn prefers_site_name_from_preview() {
        let renderer = EmbedRenderer;
        let preview = Preview {
            site_name: "Preview Site".to_string(),
            description: "Description".to_string(),
            ..Preview::default()
        };

        let spec = renderer.spec_for("https://www.example.com/path", &preview);
        assert_eq!(spec.author_name, Some("Preview Site".to_string()));
        assert_eq!(spec.description, Some("Description".to_string()));
    }
}
