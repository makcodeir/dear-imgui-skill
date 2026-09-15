// platformer_aiclient.cpp — OpenAI-compatible HTTPS client (no libcurl).
// Uses OpenSSL for TLS and POSIX sockets for transport. Builds a judge prompt,
// POSTs to /chat/completions, extracts the verdict JSON from the reply.
#include "platformer_core.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>

// ---------------------------------------------------------------------------
// JSON escaping + minimal parser (objects/arrays/strings/numbers/bools only —
// the payload is machine-generated and the verdict schema is fixed).
// ---------------------------------------------------------------------------
static std::string JsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b;
                } else o += c;
        }
    }
    return o;
}

// Extract the innermost {...} block that parses as flat JSON with the keys we
// need. LLM replies may wrap the object in prose or ```json fences.
static size_t FindStringEnd(const std::string& s, size_t start)
{
    size_t i = start;
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == '"') return i;
        i++;
    }
    return std::string::npos;
}

bool ExtractJsonObject(const std::string& text, std::string& out)
{
    size_t best = std::string::npos, best_len = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] != '{') continue;
        int depth = 0;
        bool in_str = false, esc = false;
        size_t j = i;
        for (; j < text.size(); j++) {
            char c = text[j];
            if (in_str) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') in_str = true;
            else if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) break;
            }
        }
        if (depth != 0 || in_str) continue;              // unbalanced
        if (text.find("\"is_playable\"", i) == std::string::npos) continue;
        size_t len = j - i + 1;
        if (best == std::string::npos || len < best_len) { // innermost wins
            best = i; best_len = len;
        }
    }
    if (best == std::string::npos) return false;
    out = text.substr(best, best_len);
    return true;
}

// --- tiny flat-JSON value readers (schema is ours; no recursion needed) ------
static bool JsonGetBool(const std::string& obj, const char* key, bool dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return dflt;
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return dflt;
    p++;
    while (p < obj.size() && obj[p] == ' ') p++;
    if (obj.compare(p, 4, "true") == 0) return true;
    if (obj.compare(p, 5, "false") == 0) return false;
    return dflt;
}

static float JsonGetFloat(const std::string& obj, const char* key, float dflt)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return dflt;
    p = obj.find(':', p + pat.size());
    if (p == std::string::npos) return dflt;
    return strtof(obj.c_str() + p + 1, nullptr);
}

static std::vector<std::string> JsonGetStringArray(const std::string& obj, const char* key)
{
    std::vector<std::string> out;
    std::string pat = std::string("\"") + key + "\"";
    size_t p = obj.find(pat);
    if (p == std::string::npos) return out;
    p = obj.find('[', p + pat.size());
    if (p == std::string::npos) return out;
    p++;
    while (p < obj.size() && obj[p] != ']') {
        if (obj[p] == '"') {
            size_t e = FindStringEnd(obj, p + 1);
            if (e == std::string::npos) break;
            out.push_back(obj.substr(p + 1, e - p - 1));
            p = e + 1;
        }
        p++;
    }
    return out;
}

// ---------------------------------------------------------------------------
// TLS + HTTP
// ---------------------------------------------------------------------------
struct TcpTls {
    int fd = -1;
    SSL* ssl = nullptr;
    SSL_CTX* ctx = nullptr;

    ~TcpTls() {
        if (ssl) SSL_free(ssl);
        if (ctx) SSL_CTX_free(ctx);
        if (fd >= 0) close(fd);
    }
    bool connect(const std::string& host, const std::string& port, std::string& err) {
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
            err = "dns failed: " + host;
            return false;
        }
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
            fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) continue;
            struct timeval tv{20, 0}; // 20 s I/O timeout
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
            close(fd); fd = -1;
        }
        freeaddrinfo(res);
        if (fd < 0) { err = "tcp connect failed: " + host + ":" + port; return false; }

        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { err = "SSL_CTX_new failed"; return false; }
        SSL_CTX_set_default_verify_paths(ctx);
        ssl = SSL_new(ctx);
        if (!ssl) { err = "SSL_new failed"; return false; }
        if (SSL_set_fd(ssl, fd) != 1 ||
            SSL_set_tlsext_host_name(ssl, host.c_str()) != 1 ||
            SSL_connect(ssl) != 1) {
            ERR_error_string_n(ERR_get_error(), buf_, sizeof buf_);
            err = std::string("tls failed: ") + buf_;
            return false;
        }
        return true;
    }
    ssize_t send_all(const char* d, size_t n) {
        size_t done = 0;
        while (done < n) {
            int w = SSL_write(ssl, d + done, int(n - done));
            if (w <= 0) return -1;
            done += size_t(w);
        }
        return ssize_t(done);
    }
    // Read to end of a Content-Length framed body once headers are parsed.
    bool read_response(std::string& out) {
        char chunk[16384];
        int n;
        while ((n = SSL_read(ssl, chunk, sizeof chunk)) > 0) out.append(chunk, size_t(n));
        // (loop ends on SSL_ERROR_WANT_READ == clean shutdown for our timeout cfg)
        return true;
    }
    char buf_[256] = {};
};

// Parse "https://host[:port]/path" -> host, port, path.
static bool SplitUrl(const std::string& url, std::string& host, std::string& port, std::string& path)
{
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    std::string rest = url.substr(scheme + 3);
    size_t slash = rest.find('/');
    size_t colon = rest.find(':');
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
        host = rest.substr(0, colon);
        size_t p_end = (slash == std::string::npos) ? rest.size() : slash;
        port = rest.substr(colon + 1, p_end - colon - 1);
        path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    } else {
        host = (slash == std::string::npos) ? rest : rest.substr(0, slash);
        port = "443";
        path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    }
    return !host.empty();
}

AiVerdict QueryAiPlayability(const std::string& telemetry_json,
                             const std::string& api_key_in,
                             const std::string& model,
                             const std::string& base_url)
{
    AiVerdict v;
    std::string api_key = api_key_in;
    if (api_key.empty()) {
        const char* env = std::getenv("OPENAI_API_KEY");
        if (!env) {
            v.error = "no api key: pass one or set OPENAI_API_KEY";
            return v;
        }
        api_key = env;
    }

    const std::string system_prompt =
        "You are a strict 2D platformer physics auditor. You are given JSON telemetry "
        "from a deterministic replay of a Mario-style platformer: periodic snapshots "
        "(player x/y/velocity, grounded flag, nearby tile matrix around the player, "
        "enemy positions relative to the player, distance to the goal flag) plus the "
        "complete event log (STOMPED_ENEMY, BLOCKED_BY_WALL, FALLEN_INTO_PIT, "
        "QUESTION_HIT, REACHED_GOAL, ...). Coordinates: +x is right toward the goal, "
        "+y is DOWN, gravity is positive-y. Tiles: G=ground B=brick ?=question | =pipe "
        "F=flag .=empty. The runner holds RIGHT+RUN and rhythmically jumps. Audit:\n"
        "1) Does the player make monotonic horizontal progress toward the goal?\n"
        "2) Do jump arcs physically clear the pits/pipes/obstacles shown in the tile "
        "matrices, or does the agent get stuck (repeated BLOCKED_BY_WALL, x stuck at "
        "a wall, oscillating x)?\n"
        "3) Does the run end in REACHED_GOAL, and are the deaths along the way "
        "avoidable with different inputs rather than level geometry errors?\n"
        "4) Is any part of the level insurmountable (jump cannot clear an obstacle "
        "from the available runway, pits wider than the achievable jump distance)?\n"
        "Answer with ONLY a JSON object: {\"is_playable\": bool, \"confidence\": 0.0-1.0, "
        "\"bottlenecks\": [string], \"suggested_inputs\": [string]} where bottlenecks "
        "lists concrete blockers (or [] if none) and suggested_inputs lists concrete "
        "input-script changes that would fix them (or []).";

    // Build request body
    std::string body;
    body.reserve(8192 + telemetry_json.size());
    body += "{\"model\":\"" + JsonEscape(model) + "\",\"temperature\":0,\"messages\":[";
    body += "{\"role\":\"system\",\"content\":\"" + JsonEscape(system_prompt) + "\"},";
    body += "{\"role\":\"user\",\"content\":\"" + JsonEscape(telemetry_json) + "\"}],";
    body += "\"response_format\":{\"type\":\"json_object\"}}";

    std::string host, port, path;
    std::string url = base_url.empty() ? "https://api.openai.com/v1/chat/completions" : base_url;
    if (!SplitUrl(url, host, port, path)) {
        v.error = "bad base_url: " + url;
        return v;
    }

    TcpTls t;
    std::string err;
    if (!t.connect(host, port, err)) { v.error = err; return v; }

    std::string req;
    req += "POST " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += std::string("Authorization: Bearer ") + api_key + "\r\n";
    req += "Content-Type: application/json\r\n";
    char cl[64];
    snprintf(cl, sizeof cl, "Content-Length: %zu\r\n", body.size());
    req += cl;
    req += "Connection: close\r\n\r\n";
    req += body;

    if (t.send_all(req.data(), req.size()) < 0) { v.error = "send failed"; return v; }

    std::string resp;
    if (!t.read_response(resp)) { v.error = "tls read failed"; return v; }

    // --- split headers / body ---
    size_t hdr_end = resp.find("\r\n\r\n");
    if (hdr_end == std::string::npos) { v.error = "malformed HTTP response"; return v; }
    std::string headers = resp.substr(0, hdr_end);
    std::string http_body = resp.substr(hdr_end + 4);

    // status line check
    if (headers.rfind("HTTP/1.1 200", 0) != 0 && headers.rfind("HTTP/1.0 200", 0) != 0) {
        size_t sp = headers.find(' ');
        v.error = "HTTP " + headers.substr(sp + 1, 3) + " — body: " +
                  http_body.substr(0, std::min<size_t>(400, http_body.size()));
        return v;
    }

    // Chunked transfer decoding (OpenAI streams chunked when Connection: close)
    {
        std::string lh = headers;
        std::transform(lh.begin(), lh.end(), lh.begin(), ::tolower);
        if (lh.find("transfer-encoding: chunked") != std::string::npos) {
            std::string decoded;
            size_t rp = 0;
            while (rp < http_body.size()) {
                size_t nl = http_body.find("\r\n", rp);
                if (nl == std::string::npos) break;
                size_t sz = size_t(strtoul(http_body.c_str() + rp, nullptr, 16));
                if (sz == 0) break;
                decoded += http_body.substr(nl + 2, sz);
                rp = nl + 2 + sz + 2;
            }
            http_body = decoded;
        }
    }

    // Pull the assistant message content out of the API envelope.
    std::string content;
    {
        std::string key = "\"content\":\"";
        size_t p = http_body.find(key);
        if (p == std::string::npos) {
            v.error = "no content field in response: " +
                      http_body.substr(0, std::min<size_t>(400, http_body.size()));
            return v;
        }
        p += key.size();
        size_t e = FindStringEnd(http_body, p);
        content = http_body.substr(p, e - p);
        // unescape the common JSON escapes
        std::string un;
        for (size_t i = 0; i < content.size(); i++) {
            if (content[i] == '\\' && i + 1 < content.size()) {
                char n = content[++i];
                switch (n) {
                    case 'n': un += '\n'; break;
                    case 't': un += '\t'; break;
                    case 'r': un += '\r'; break;
                    default: un += n;
                }
            } else un += content[i];
        }
        content = un;
    }
    v.raw_response = content;

    std::string obj;
    if (!ExtractJsonObject(content, obj)) {
        v.error = "no verdict JSON in model reply: " + content.substr(0, 200);
        return v;
    }
    v.is_playable = JsonGetBool(obj, "is_playable", false);
    v.confidence = JsonGetFloat(obj, "confidence", 0.f);
    v.bottlenecks = JsonGetStringArray(obj, "bottlenecks");
    v.suggested_inputs = JsonGetStringArray(obj, "suggested_inputs");
    v.ok = true;
    return v;
}
