#pragma once
#include "web_protocol.hpp"
#include "input/remote_input_queue.hpp"
#include <atomic>
#include <memory>

namespace render_module::video { class VideoPipeline; }
namespace render_module::detail {
struct WebCounters {
    std::atomic<unsigned> sessions{0};
    std::atomic<bool> controller{false};
    std::atomic<std::uint64_t> rendered{0}, encoded{0}, dropped{0}, bytes{0};
    std::atomic<std::uint64_t> accepted{0}, rejected{0}, full{0}, encodeMicros{0};
};
// No server-library types, GL handles, or ImGui objects cross this boundary.
class WebServer {
public:
    WebServer(WebConfig config, std::shared_ptr<RemoteInputQueue> input,
              std::shared_ptr<video::VideoPipeline> video = {});
    ~WebServer();
    bool Start();
    void Stop();
    unsigned Port() const;
    bool HasViewers() const { return counters.sessions.load() != 0; }
    bool TakeViewport(WebSize& size);
    void ObserveViewport(WebSize size);
    void Publish(std::shared_ptr<const std::vector<unsigned char>> packet, std::uint64_t id);
    WebCounters counters;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string_view WebAsset(std::string_view path);
} // namespace render_module::detail
