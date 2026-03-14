use tl::ParserOptions;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Metadata {
    pub title: String,
    pub image_url: String,
    pub description: String,
    pub site_name: String,
}

pub fn parse_metadata(html: &str) -> Option<Metadata> {
    let dom = tl::parse(html, ParserOptions::default()).ok()?;
    let parser = dom.parser();
    let mut metadata = Metadata::default();
    let mut fallback_title = String::new();

    if let Some(mut titles) = dom.query_selector("title") {
        if let Some(handle) = titles.next() {
            if let Some(node) = handle.get(parser) {
                if let Some(tag) = node.as_tag() {
                    fallback_title = tag.inner_text(parser).trim().to_string();
                }
            }
        }
    }

    if let Some(tags) = dom.query_selector("meta") {
        for handle in tags {
            let Some(node) = handle.get(parser) else {
                continue;
            };
            let Some(tag) = node.as_tag() else {
                continue;
            };

            let key = attribute(tag, "property")
                .or_else(|| attribute(tag, "name"))
                .map(|value| value.to_ascii_lowercase());
            let content = attribute(tag, "content");

            let (Some(key), Some(content)) = (key, content) else {
                continue;
            };

            match key.as_str() {
                "og:title" | "twitter:title" if metadata.title.is_empty() => metadata.title = content,
                "og:description" | "twitter:description" | "description"
                    if metadata.description.is_empty() =>
                {
                    metadata.description = content
                }
                "og:image" | "og:image:url" | "og:image:secure_url" | "twitter:image"
                | "twitter:image:src"
                    if metadata.image_url.is_empty() =>
                {
                    metadata.image_url = content
                }
                "og:site_name" if metadata.site_name.is_empty() => metadata.site_name = content,
                _ => {}
            }
        }
    }

    if metadata.title.is_empty() && !fallback_title.is_empty() {
        metadata.title = fallback_title;
    }

    if metadata.title.is_empty()
        && metadata.description.is_empty()
        && metadata.image_url.is_empty()
        && metadata.site_name.is_empty()
    {
        None
    } else {
        Some(metadata)
    }
}

fn attribute(tag: &tl::HTMLTag<'_>, key: &str) -> Option<String> {
    tag.attributes()
        .get(key)
        .flatten()
        .map(|value| value.as_utf8_str().trim().to_string())
        .filter(|value| !value.is_empty())
}

#[cfg(test)]
mod tests {
    use super::{parse_metadata, Metadata};

    #[test]
    fn extracts_open_graph_and_title_fields() {
        let html = r#"
            <html>
              <head>
                <title>Fallback title</title>
                <meta property="og:description" content="desc" />
                <meta property="og:image" content="/img.png" />
                <meta property="og:site_name" content="Example" />
              </head>
            </html>
        "#;

        let metadata = parse_metadata(html).unwrap();
        assert_eq!(
            metadata,
            Metadata {
                title: "Fallback title".to_string(),
                description: "desc".to_string(),
                image_url: "/img.png".to_string(),
                site_name: "Example".to_string(),
            }
        );
    }
}
