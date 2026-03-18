use regex::Regex;

use crate::engine::ImageProxyConfig;

pub fn extract_urls(text: &str) -> Vec<String> {
    static URL_RE: std::sync::OnceLock<Regex> = std::sync::OnceLock::new();
    let regex = URL_RE.get_or_init(|| Regex::new(r#"(https?://[^\s<>"']+)"#).expect("valid regex"));

    regex
        .captures_iter(text)
        .filter_map(|capture| capture.get(1))
        .map(|value| clean_url(value.as_str().to_string()))
        .filter(|value| !value.is_empty())
        .collect()
}

pub fn resolve_against(base_url: &str, candidate: &str) -> String {
    if candidate.is_empty() {
        return candidate.to_string();
    }

    if starts_with(candidate, "http://") || starts_with(candidate, "https://") {
        return candidate.to_string();
    }

    if starts_with(candidate, "//") {
        return format!("https:{candidate}");
    }

    let scheme_host = get_scheme_host(base_url);
    if scheme_host.is_empty() {
        return candidate.to_string();
    }

    if candidate.starts_with('/') {
        return format!("{scheme_host}{candidate}");
    }

    let base_dir = get_base_dir(base_url);
    if base_dir.is_empty() {
        return candidate.to_string();
    }

    if base_dir.ends_with('/') {
        format!("{base_dir}{candidate}")
    } else {
        format!("{base_dir}/{candidate}")
    }
}

pub fn proxy_image_if_needed(config: &ImageProxyConfig, image_url: &str) -> String {
    if image_url.is_empty() || !config.enabled {
        return image_url.to_string();
    }

    let host = extract_host_lower(image_url);
    if host.is_empty() {
        return image_url.to_string();
    }

    let matched = config
        .hosts
        .iter()
        .any(|pattern| !pattern.is_empty() && host.contains(&pattern.to_ascii_lowercase()))
        || image_url.contains("viewimage.php");

    if !matched {
        return image_url.to_string();
    }

    let mut base = if config.base.is_empty() {
        "https://images.weserv.nl".to_string()
    } else {
        config.base.clone()
    };

    while base.ends_with('/') {
        base.pop();
    }

    let mut proxied = format!("{base}?url={}", url_encode_all(image_url));
    if !config.query.is_empty() {
        proxied.push('&');
        proxied.push_str(&config.query);
    }

    proxied
}

fn clean_url(mut value: String) -> String {
    while value
        .chars()
        .last()
        .is_some_and(|ch| matches!(ch, ')' | ']' | ',' | '.' | '!' | '?' | ';' | ':'))
    {
        value.pop();
    }

    while value.ends_with(')') && count_char(&value, ')') > count_char(&value, '(') {
        value.pop();
    }

    value
}

fn count_char(text: &str, needle: char) -> usize {
    text.chars().filter(|ch| *ch == needle).count()
}

fn starts_with(value: &str, prefix: &str) -> bool {
    value.len() >= prefix.len() && value[..prefix.len()].eq(prefix)
}

fn get_scheme_host(url: &str) -> String {
    let Some(scheme_pos) = url.find("://") else {
        return String::new();
    };

    let start_host = scheme_pos + 3;
    let end_pos = url[start_host..]
        .find(['/', '\\', '?', '#'])
        .map(|offset| start_host + offset)
        .unwrap_or(url.len());

    url[..end_pos].to_string()
}

fn get_base_dir(url: &str) -> String {
    let scheme_host = get_scheme_host(url);
    if scheme_host.is_empty() {
        return String::new();
    }

    let mut rest = url[scheme_host.len()..].to_string();
    if let Some(pos) = rest.find(['?', '#']) {
        rest.truncate(pos);
    }

    if rest.is_empty() {
        rest.push('/');
    } else if !rest.ends_with('/') {
        if let Some(pos) = rest.rfind('/') {
            rest.truncate(pos + 1);
        } else {
            rest = "/".to_string();
        }
    }

    format!("{scheme_host}{rest}")
}

fn extract_host_lower(url: &str) -> String {
    let Some(scheme_pos) = url.find("://") else {
        return String::new();
    };

    let start = scheme_pos + 3;
    let end = url[start..]
        .find(['/', '\\', '?', '#'])
        .map(|offset| start + offset)
        .unwrap_or(url.len());

    url[start..end].to_ascii_lowercase()
}

fn url_encode_all(value: &str) -> String {
    let mut out = String::with_capacity(value.len() * 3);

    for byte in value.bytes() {
        if byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b'~') {
            out.push(byte as char);
        } else {
            out.push('%');
            out.push(hex((byte >> 4) & 0x0F));
            out.push(hex(byte & 0x0F));
        }
    }

    out
}

fn hex(nibble: u8) -> char {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    HEX[nibble as usize] as char
}

#[cfg(test)]
mod tests {
    use super::{extract_urls, resolve_against};

    #[test]
    fn cleans_trailing_punctuation_and_parentheses() {
        let urls =
            extract_urls("Check this out: (https://example.com/path). And https://foo.bar/baz).");
        assert_eq!(urls[0], "https://example.com/path");
        assert_eq!(urls[1], "https://foo.bar/baz");
    }

    #[test]
    fn resolves_relative_urls() {
        let base = "https://example.com/path/page.html";
        assert_eq!(
            resolve_against(base, "https://cdn.example.com/a.jpg"),
            "https://cdn.example.com/a.jpg"
        );
        assert_eq!(
            resolve_against(base, "//cdn.example.com/a.jpg"),
            "https://cdn.example.com/a.jpg"
        );
        assert_eq!(
            resolve_against(base, "/img/a.png"),
            "https://example.com/img/a.png"
        );
        assert_eq!(
            resolve_against(base, "img/a.png"),
            "https://example.com/path/img/a.png"
        );
        assert!(resolve_against("https://example.com", "img.png").starts_with("https://example.com/"));
    }
}
