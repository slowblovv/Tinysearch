#include "tinysearch/http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

namespace tinysearch {

namespace {

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = std::stoi(s.substr(i + 1, 2), nullptr, 16);
            out += static_cast<char>(hi);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

void parse_target(const std::string& target, std::string& path,
                   std::map<std::string, std::string>& query) {
    std::size_t q = target.find('?');
    path = url_decode(q == std::string::npos ? target : target.substr(0, q));
    if (q == std::string::npos) return;
    std::string qs = target.substr(q + 1);
    std::size_t pos = 0;
    while (pos < qs.size()) {
        std::size_t amp = qs.find('&', pos);
        std::string pair = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        std::size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            query[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            query[url_decode(pair)] = "";
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
}

}  // namespace

HttpServer::HttpServer(std::string host, int port) : host_(std::move(host)), port_(port) {}
HttpServer::~HttpServer() { stop(); }

void HttpServer::on(const std::string& method, const std::string& path, Handler handler) {
    exact_handlers_[method + " " + path] = std::move(handler);
}

void HttpServer::on_prefix(const std::string& method, const std::string& prefix, Handler handler) {
    prefix_handlers_.emplace_back(method + " " + prefix, std::move(handler));
}

HttpResponse HttpServer::dispatch(const HttpRequest& req) {
    auto exact = exact_handlers_.find(req.method + " " + req.path);
    if (exact != exact_handlers_.end()) return exact->second(req);

    for (auto& [key, handler] : prefix_handlers_) {
        std::size_t sp = key.find(' ');
        std::string method = key.substr(0, sp);
        std::string prefix = key.substr(sp + 1);
        if (req.method == method && req.path.size() > prefix.size() &&
            req.path.compare(0, prefix.size(), prefix) == 0) {
            HttpRequest sub = req;
            sub.path = req.path.substr(prefix.size());
            return handler(sub);
        }
    }
    return HttpResponse::json(404, R"({"error":"not found"})");
}

void HttpServer::handle_connection(int client_fd) {
    std::string buffer;
    char chunk[4096];
    ssize_t n;

    // Read the request line + headers first.
    std::size_t header_end = std::string::npos;
    while ((n = ::recv(client_fd, chunk, sizeof(chunk), 0)) > 0) {
        buffer.append(chunk, static_cast<std::size_t>(n));
        header_end = buffer.find("\r\n\r\n");
        if (header_end != std::string::npos) break;
        if (buffer.size() > (1u << 20)) break;  // 1 MB header guard
    }

    if (header_end == std::string::npos) {
        ::close(client_fd);
        return;
    }

    std::istringstream head(buffer.substr(0, header_end));
    std::string request_line;
    std::getline(head, request_line);
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();

    std::istringstream rl(request_line);
    std::string method, target, version;
    rl >> method >> target >> version;

    std::size_t content_length = 0;
    std::string line;
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (key == "content-length") {
            std::string val = line.substr(colon + 1);
            std::size_t a = val.find_first_not_of(' ');
            if (a != std::string::npos) content_length = std::stoul(val.substr(a));
        }
    }

    std::string body = buffer.substr(header_end + 4);
    while (body.size() < content_length && (n = ::recv(client_fd, chunk, sizeof(chunk), 0)) > 0) {
        body.append(chunk, static_cast<std::size_t>(n));
    }

    HttpRequest req;
    req.method = method;
    req.body = body;
    parse_target(target, req.path, req.query);

    HttpResponse resp;
    try {
        resp = dispatch(req);
    } catch (const std::exception& e) {
        resp = HttpResponse::json(500, std::string(R"({"error":")") + e.what() + "\"}");
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status << " " << (resp.status == 200 ? "OK" : "Error") << "\r\n";
    out << "Content-Type: " << resp.content_type << "\r\n";
    out << "Content-Length: " << resp.body.size() << "\r\n";
    out << "Connection: close\r\n\r\n";
    out << resp.body;
    std::string out_str = out.str();
    ::send(client_fd, out_str.data(), out_str.size(), 0);
    ::close(client_fd);
}

void HttpServer::run() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (host_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        ::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed on " + host_ + ":" + std::to_string(port_));
    }
    if (::listen(listen_fd_, 64) < 0) {
        throw std::runtime_error("listen() failed");
    }

    running_ = true;
    std::cout << "TinySearch listening on http://" << host_ << ":" << port_ << "\n";

    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_fd_, &fds);
        timeval tv{0, 200000};  // 200ms, so we can re-check `running_` for shutdown
        int rv = ::select(listen_fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (rv <= 0) continue;

        int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) continue;

        // Thread-per-request (spec section 52, v1-acceptable strategy).
        // Index mutation vs. search concurrency is handled inside
        // SearchEngine's shared_mutex, not here.
        std::thread(&HttpServer::handle_connection, this, client_fd).detach();
    }

    ::close(listen_fd_);
    listen_fd_ = -1;
    std::cout << "TinySearch server stopped.\n";
}

void HttpServer::stop() {
    running_ = false;
}

}  // namespace tinysearch
