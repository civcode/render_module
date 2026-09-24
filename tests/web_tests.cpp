#include "web/web_server.hpp"
#include "web/jpeg_encode.h"
#include "render_module/render_module.hpp"
#include "core/root_framebuffer.hpp"
#include "present/image_presenter.hpp"
#include "present/web_presenter.hpp"
#include "input/input_access.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <stb_image.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace render_module::detail;
using render_module::WebConfig;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ws = beast::websocket;
using tcp = net::ip::tcp;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while(false)
namespace {
constexpr auto token = "0123456789abcdef0123456789abcdef";
template<class F> void Wait(F condition) {
    for (int n = 0; n < 400; ++n) { if (condition()) return; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    CHECK(false);
}
http::response<http::string_body> Get(unsigned port, std::string path, bool auth = false) {
    net::io_context io; beast::tcp_stream stream(io);
    stream.connect({net::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
    http::request<http::string_body> req{http::verb::get, path, 11}; req.set(http::field::host, "127.0.0.1");
    if (auth) req.set(http::field::authorization, std::string("Bearer ")+token);
    http::write(stream, req); beast::flat_buffer buffer; http::response<http::string_body> response;
    http::read(stream, buffer, response); return response;
}
struct Client {
    net::io_context io;
    ws::stream<beast::tcp_stream> socket{io};
    std::uint64_t seq = 0;
    http::response<http::string_body> response;
    bool Connect(unsigned port, bool auth = true, bool origin = true) {
        beast::get_lowest_layer(socket).connect({net::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
        socket.set_option(ws::stream_base::decorator([=](ws::request_type& r) {
            r.set(http::field::origin, origin ? "http://127.0.0.1:"+std::to_string(port) : "https://evil.invalid");
            if (auth) r.set(http::field::authorization, std::string("Bearer ")+token);
        }));
        beast::error_code error; socket.handshake(response, "127.0.0.1", "/api/ws", error); return !error;
    }
    void Send(boost::json::object message) {
        message["v"] = 1; message["seq"] = ++seq; socket.text(true); socket.write(net::buffer(boost::json::serialize(message)));
    }
    void Raw(const std::string& text) { socket.text(true); socket.write(net::buffer(text)); }
    std::vector<unsigned char> Read(bool& binary) {
        beast::flat_buffer buffer; socket.read(buffer); binary = socket.got_binary();
        const auto s = beast::buffers_to_string(buffer.data()); return {s.begin(), s.end()};
    }
    boost::json::object Until(std::string_view type) {
        for (int n = 0; n < 30; ++n) {
            bool binary; auto data = Read(binary); if (binary) continue;
            auto value = boost::json::parse(std::string(data.begin(), data.end()));
            if (value.as_object().at("type").as_string() == type) return value.as_object();
        }
        throw std::runtime_error("missing control message");
    }
    std::uint64_t Frame() {
        for (int n = 0; n < 30; ++n) {
            bool binary; auto data = Read(binary); if (!binary) continue;
            CHECK(data.size() >= 28 && std::memcmp(data.data(), "RMJP", 4) == 0);
            std::uint64_t id = 0; for (int i = 8; i < 16; ++i) id = (id << 8) | data[i]; return id;
        }
        throw std::runtime_error("missing frame");
    }
    void Closed() {
        beast::error_code error; beast::flat_buffer buffer;
        for (int n = 0; n < 30 && !error; ++n) { socket.read(buffer, error); buffer.consume(buffer.size()); }
        CHECK(error);
    }
    void Close() { beast::error_code error; if (socket.is_open()) socket.close(ws::close_code::normal, error); }
};
boost::json::object KeyMessage(bool down) {
    return {{"type", "key"}, {"key", "A"}, {"down", down}, {"repeat", false},
        {"mods", {{"ctrl", false}, {"shift", false}, {"alt", false}, {"super", false}}}};
}
void ServerTests() {
    auto queue = std::make_shared<RemoteInputQueue>(); WebConfig config; config.port = 0; config.authToken = token; config.maxClients = 3;
    { auto bad = config; bad.bindAddress = "0.0.0.0"; bad.authToken.clear(); WebServer denied(bad, queue); CHECK(!denied.Start()); }
    WebServer server(config, queue); CHECK(server.Start()); const auto port = server.Port(); CHECK(port);
    CHECK(Get(port, "/").result_int() == 200);
    CHECK(Get(port, "/app.js").body().find("normalizedPoint") != std::string::npos);
    CHECK(Get(port, "/style.css").result_int() == 200);
    CHECK(Get(port, "/healthz").body() == "{\"ok\":true}"); // No render loop/context exists.
    CHECK(Get(port, "/api/version").result_int() == 401);
    auto version = Get(port, "/api/version", true);
    CHECK(version.result_int() == 200 && !version["Content-Security-Policy"].empty());
    CHECK(version.body().find(token) == std::string::npos);
    { Client bad; CHECK(!bad.Connect(port, false)); CHECK(bad.response.result_int() == 401); }
    { Client bad; CHECK(!bad.Connect(port, true, false)); CHECK(bad.response.result_int() == 403); }
    Client controller, viewer;
    CHECK(controller.Connect(port)); auto welcome = controller.Until("welcome"); CHECK(welcome.at("control").as_bool());
    CHECK(welcome.at("session").as_string().size() == 32); CHECK(queue->TakeReleaseAll());
    CHECK(viewer.Connect(port)); auto watch = viewer.Until("welcome"); CHECK(!watch.at("control").as_bool());
    CHECK(watch.at("session") != welcome.at("session"));
    viewer.Send(KeyMessage(true)); viewer.Until("view_only"); CHECK(queue->Size() == 0);
    controller.Send(KeyMessage(true)); Wait([&] { return queue->Size() == 1; });
    QueuedInput event; CHECK(queue->TryPop(event) && std::get<Key>(event.event).down);
    for (std::size_t i = 0; i < RemoteInputQueue::Capacity; ++i) CHECK(queue->Enqueue(Key{RenderKey::A, true}).Accepted());
    controller.Send(KeyMessage(false)); controller.Until("input_reset");
    CHECK(server.counters.full == 1 && queue->TakeReleaseAll() && queue->Size() == 0);
    controller.Send({{"type", "viewport"}, {"width", 4000}, {"height", 2000}, {"devicePixelRatio", 2}});
    WebSize requested; Wait([&] { return server.TakeViewport(requested); });
    CHECK(requested.width == 1920 && requested.height == 960); // DPR is not multiplied.
    for (int n = 0; n < 50; ++n)
        controller.Send({{"type", "viewport"}, {"width", 300+n}, {"height", 200+n}, {"devicePixelRatio", 1}});
    Wait([&] { return server.TakeViewport(requested) && requested.width == 349 && requested.height == 249; });
    const unsigned char fakeJpeg[] = {0xff, 0xd8, 0xff, 0xd9};
    server.Publish(std::make_shared<const std::vector<unsigned char>>(PackJpeg(1, 320, 200, fakeJpeg, 4)), 1);
    CHECK(controller.Frame() == 1);
    for (std::uint64_t id = 2; id <= 100; ++id)
        server.Publish(std::make_shared<const std::vector<unsigned char>>(PackJpeg(id, 320, 200, fakeJpeg, 4)), id);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    controller.Send({{"type", "frame_ack"}, {"frameId", 1}});
    CHECK(controller.Frame() == 100); CHECK(server.counters.dropped > 0);
    controller.Send({{"type", "frame_ack"}, {"frameId", 100}});
    for (int n = 0; n < 12; ++n) {
        Client temporary; CHECK(temporary.Connect(port)); temporary.Until("welcome");
        if (n == 0) { Client extra; CHECK(!extra.Connect(port)); CHECK(extra.response.result_int() == 503); }
        temporary.Close();
    }
    controller.Send({{"type", "viewport"}, {"width", 777}, {"height", 555}, {"devicePixelRatio", 1}});
    controller.Send(KeyMessage(true)); Wait([&] { return queue->Size() != 0; });
    controller.Close(); auto promoted = viewer.Until("lease"); CHECK(promoted.at("control").as_bool());
    CHECK(!server.TakeViewport(requested)); // Old controller's pending resize is canceled.
    CHECK(queue->TakeReleaseAll()); viewer.Close(); Wait([&] { return server.counters.sessions == 0; });
    CHECK(queue->TakeReleaseAll());
    for (const std::string& invalid : {std::string("{"),
         std::string(R"({"v":1,"seq":1,"type":"mouse_move","x":2,"y":0,"source":"mouse"})"),
         std::string(R"({"v":1,"seq":1,"type":"key","key":"Bogus","down":true})"),
         std::string(R"({"v":1,"seq":1,"type":"viewport","width":0,"height":20,"devicePixelRatio":1})"),
         std::string(R"({"v":1,"seq":1,"type":"text","text":"\ud800"})"),
         std::string("\xff"), std::string(WebMessageLimit+1, 'x')}) {
        Client bad; CHECK(bad.Connect(port)); bad.Until("welcome"); bad.Raw(invalid); bad.Closed();
        Wait([&] { return server.counters.sessions == 0; });
    }
    Client slow; CHECK(slow.Connect(port)); slow.Until("welcome");
    std::vector<unsigned char> large(1024*1024, 42);
    server.Publish(std::make_shared<const std::vector<unsigned char>>(PackJpeg(101, 320, 200, large.data(), large.size())), 101);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const auto stopStart = std::chrono::steady_clock::now();
    server.Stop(); CHECK(server.counters.sessions == 0);
    CHECK(std::chrono::steady_clock::now()-stopStart < std::chrono::seconds(2));
}
void ProtocolTests() {
    WebConfig config; WebInputState state; WebMessage result;
    CHECK(ParseWebMessage(R"({"v":1,"seq":1,"type":"text","text":"äöüÄÖÜß 日本 🙂"})", config, state, result));
    CHECK(std::holds_alternative<TextUtf8>(result.events.back()));
    CHECK(ParseWebMessage(R"({"v":1,"seq":1,"type":"frame_ack","frameId":"18446744073709551615"})", config, state, result));
    CHECK(result.frameId == UINT64_MAX);
    CHECK(!ParseWebMessage(R"({"v":2,"seq":1,"type":"focus","focused":true})", config, state, result));
    CHECK(!ParseWebMessage(R"({"v":1,"seq":0,"type":"focus","focused":true})", config, state, result));
    CHECK(!ParseWebMessage(R"({"v":1,"seq":1,"type":"mouse_move","x":1e999,"y":0,"source":"mouse"})", config, state, result));
    CHECK(!ParseWebMessage(R"({"v":1,"seq":1,"type":"wheel","horizontal":0,"vertical":1001})", config, state, result));
    CHECK(!ParseWebMessage(std::string("{\"v\":1,\"seq\":1,\"type\":\"text\",\"text\":\"")+std::string(257, 'a')+"\"}", config, state, result));
    auto size = ClampWebViewport(4000, 1000, config); CHECK(size.width == 1920 && size.height == 480);
    CHECK(ClampWebViewport(0, 100, config).width == 0);
    CHECK(ClampWebViewport(20000, 100, config).width == 0);
    const unsigned char bytes[] = {1,2,3}; auto packet = PackJpeg(0x0102030405060708ULL, 640, 480, bytes, 3);
    CHECK(packet.size() == 31 && packet[8] == 1 && packet[15] == 8 && packet[24] == 0 && packet[27] == 3);
    CHECK(PackJpeg(0, 640, 480, bytes, 3).empty());
}
void JpegTests() {
    render_module::Config config; config.backend = render_module::Backend::Headless;
    config.headlessContext = render_module::HeadlessContext::NativeEgl;
    config.width = 128; config.height = 96; config.web.port = 0; CHECK(RenderModule::Init(config));
    auto presenter = CreateWebPresenter(config, RemoteInputQueueHandle()); CHECK(presenter);
    RootFramebuffer root; CHECK(root.Resize(64, 48)); root.BeginFrame();
    glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST); glScissor(0, 24, 64, 24); glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    const auto now = std::chrono::steady_clock::now(); auto frame = root.Complete(1, now, now);
    ImageRgba image; CHECK(ImagePresenter::Read(frame, image));
    unsigned char* jpeg = nullptr; unsigned long length = 0;
    CHECK(rm_encode_jpeg(image.pixels.data(), 64, 48, 90, WebFrameLimit, &jpeg, &length));
    int w, h, channels; auto* decoded = stbi_load_from_memory(jpeg, int(length), &w, &h, &channels, 3);
    CHECK(decoded && w == 64 && h == 48);
    CHECK(decoded[(5*64+32)*3] > 240 && decoded[(42*64+32)*3+2] > 240);
    stbi_image_free(decoded); std::free(jpeg);
    std::vector<unsigned char> noise(128*128*4); std::uint32_t random = 1;
    for (auto& byte : noise) { random = random*1664525u + 1013904223u; byte = random >> 24; }
    for (int n = 0; n < 10; ++n) for (unsigned long cap : {0UL, 512UL, 8192UL}) {
        // 8192 forces growth before failure; output ownership must remain sound.
        CHECK(!rm_encode_jpeg(noise.data(), 128, 128, 100, cap, &jpeg, &length));
        CHECK(jpeg == nullptr && length == 0);
    }
    CHECK(rm_encode_jpeg(noise.data(), 128, 128, 100, WebFrameLimit, &jpeg, &length));
    CHECK(length > 8192); std::free(jpeg);
    for (int n = 0; n < 20; ++n) {
        auto old = frame; CHECK(root.Resize(65+n, 49+n)); CHECK(!old.IsValid());
        CHECK(!ImagePresenter::Read(old, image)); CHECK(!presenter->Present(old));
        root.BeginFrame(); glClear(GL_COLOR_BUFFER_BIT);
        frame = root.Complete(2+n, now, now); CHECK(frame.width == 65+n && frame.framebufferGeneration == unsigned(n+2));
    }
    CHECK(glGetError() == GL_NO_ERROR); presenter.reset(); root.Destroy(); RenderModule::Shutdown();
}
}
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        if (std::strcmp(argv[1], "server") == 0) ServerTests();
        else if (std::strcmp(argv[1], "protocol") == 0) ProtocolTests(); else JpegTests();
        std::cout << "Web tests passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; RenderModule::Shutdown(); return 1; }
}
