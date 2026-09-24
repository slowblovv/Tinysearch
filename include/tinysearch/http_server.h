#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tinysearch {

// TinySearch deliberately does not depend on cpp-httplib or any other HTTP
// framework (spec section 4 allows one, but also says "make the HTTP layer
// minimal and not spend much time on it" — and no third-party headers were
// available to vendor in this environment). This is a small blocking,
// thread-per-connection POSIX-socket HTTP/1.1 server: just enough GET/POST,
// headers and query-string parsing to serve the five endpoints in
// docs/architecture.md. It is not meant to be a general-purpose web server.
struct HttpRequest {
    std::string method;
    std::string path;                              // decoded, without query string
    std::map<std::string, std::string> query;       // decoded query params
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;

    static HttpResponse json(int status, std::string body) {
        return HttpResponse{status, "application/json", std::move(body)};
    }
};

using Handler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    HttpServer(std::string host, int port);
    ~HttpServer();

    // Exact-path handlers, keyed by "METHOD /path", e.g. "GET /health".
    void on(const std::string& method, const std::string& path, Handler handler);

    // Handler for paths with a single trailing segment, e.g. GET
    // "/documents/{id}" -> registered as prefix "/documents/", handler
    // receives the remainder as req.path.
    void on_prefix(const std::string& method, const std::string& prefix, Handler handler);

    // Blocks, serving requests, until stop() is called (e.g. from a SIGINT
    // handler) — see spec section 73, graceful shutdown.
    void run();
    void stop();

private:
    std::string host_;
    int port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::map<std::string, Handler> exact_handlers_;
    std::vector<std::pair<std::string, Handler>> prefix_handlers_;  // "METHOD /prefix/"

    void handle_connection(int client_fd);
    HttpResponse dispatch(const HttpRequest& req);
};

}  // namespace tinysearch
