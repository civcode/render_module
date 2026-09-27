#include "web/web_server.hpp"
#include "web/jpeg_encode.h"
#include "web/image_encoder.hpp"
#include "render_module/render_module.hpp"
#include "core/root_framebuffer.hpp"
#include "present/image_presenter.hpp"
#include "present/web_presenter.hpp"
#include "input/input_access.hpp"
#ifdef WEBRTC_TEST
#include "render_module/video.hpp"
#endif
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
            CHECK(data.size()>=28 && std::memcmp(data.data(),"RMIM",4)==0 && data[4]==0 && data[5]==2);
            CHECK(data[6]==0 && (data[7]==1 || data[7]==2));
            std::uint64_t id=0; for(int i=8;i<16;++i) id=(id<<8)|data[i]; return id;
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
    { auto bad=config; bad.bindAddress="0.0.0.0"; bad.authToken.clear(); WebServer denied(bad,queue); CHECK(!denied.Start()); }
    { auto bad=config;bad.imageCodec=static_cast<render_module::WebSocketImageCodec>(99);WebServer denied(bad,queue);CHECK(!denied.Start()); }
    { auto bad=config;bad.jpegQuality=0;WebServer denied(bad,queue);CHECK(!denied.Start()); }
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
    CHECK(controller.Connect(port));auto welcome=controller.Until("welcome");CHECK(welcome.at("control").as_bool());
    CHECK(welcome.at("transport")=="websocket-image" && welcome.at("imageCodec")=="jpeg");
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
    EncodedImage fake; fake.codec=ImageCodec::Jpeg; fake.width=320; fake.height=200;
    fake.bytes={0xff,0xd8,0xff,0xd9}; fake.frameId=1;
    server.Publish(std::make_shared<const std::vector<unsigned char>>(PackImage(fake)),1);
    CHECK(controller.Frame() == 1);
    for (std::uint64_t id = 2; id <= 100; ++id)
        { fake.frameId=id; server.Publish(std::make_shared<const std::vector<unsigned char>>(PackImage(fake)),id); }
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
    fake.frameId=101;fake.bytes=std::move(large);
    server.Publish(std::make_shared<const std::vector<unsigned char>>(PackImage(fake)),101);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const auto stopStart = std::chrono::steady_clock::now();
    server.Stop();CHECK(server.counters.sessions==0);
    CHECK(std::chrono::steady_clock::now()-stopStart<std::chrono::seconds(2));
    auto pngConfig=config;pngConfig.imageCodec=render_module::WebSocketImageCodec::Png;pngConfig.jpegQuality=0;
    WebServer pngServer(pngConfig,queue);CHECK(pngServer.Start());Client pngClient;CHECK(pngClient.Connect(pngServer.Port()));
    const auto pngWelcome=pngClient.Until("welcome");CHECK(pngWelcome.at("imageCodec")=="png");
    EncodedImage png;png.codec=ImageCodec::Png;png.frameId=1;png.width=32;png.height=24;
    png.bytes={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    pngServer.Publish(std::make_shared<const std::vector<unsigned char>>(PackImage(png)),1);CHECK(pngClient.Frame()==1);
    const auto pngMetrics=boost::json::parse(Get(pngServer.Port(),"/api/version",true).body()).as_object();
    CHECK(pngMetrics.at("image_codec")=="png");pngClient.Close();pngServer.Stop();
}
#ifdef WEBRTC_TEST
void SignalingTests() {
    auto queue=std::make_shared<RemoteInputQueue>();
    auto pipeline=std::make_shared<render_module::video::VideoPipeline>(); CHECK(pipeline->Configure({}));
    WebConfig config; config.port=0; config.authToken=token; config.transport=render_module::WebTransport::WebRtc;
    WebServer server(config,queue,pipeline); CHECK(server.Start()); const auto port=server.Port();
    { Client bad; CHECK(!bad.Connect(port,false)); CHECK(bad.response.result_int()==401); }
    { Client bad; CHECK(!bad.Connect(port,true,false)); CHECK(bad.response.result_int()==403); }
    Client owner; CHECK(owner.Connect(port)); const auto welcome=owner.Until("welcome");
    CHECK(welcome.at("transport")=="webrtc" && welcome.at("imageCodec")=="inactive"); const auto ownerId=welcome.at("session");
    owner.Send({{"type","hello"},{"session",ownerId}});owner.Until("hello");owner.Until("offer");
    CHECK(queue->TakeReleaseAll());
    // Seed the existing input queue to check cancellation isolation. WebRTC WS input is forbidden.
    CHECK(queue->Enqueue(Key{RenderKey::A,true}).Accepted());
    for(int mode=0;mode<11;++mode) {
        Client bad; CHECK(bad.Connect(port)); const auto id=bad.Until("welcome").at("session");
        if(mode==0) bad.Send({{"type","hello"},{"session",ownerId}}); // Wrong socket ownership.
        else if(mode==1) bad.Send({{"type","answer"},{"session",id},{"sdp","invalid"}});
        else if(mode==2) bad.Raw(std::string(WebSignalingLimit+1,'x'));
        else if(mode==3) bad.Send({{"type","text"},{"text",std::string(WebMessageLimit,'x')}});
        else {
            bad.Send({{"type","hello"},{"session",id}});bad.Until("hello");bad.Until("offer");
            if(mode==4) bad.Send({{"type","hello"},{"session",id}});
            if(mode==5) bad.Send({{"type","answer"},{"session",id},{"sdp","invalid"}});
            if(mode==6) bad.Send({{"type","ice-candidate"},{"session",id},{"candidate",""},{"mid","video"}});
            if(mode==7) bad.Send({{"type","ice-candidate"},{"session",id},{"candidate",std::string(1025,'x')},{"mid","video"}});
            if(mode==8) {bad.Send({{"type","ice-complete"},{"session",id}});bad.Send({{"type","ice-complete"},{"session",id}});}
            if(mode==9) {bad.Send({{"type","ice-complete"},{"session",id}});bad.Send({{"type","ice-candidate"},{"session",id},
                {"candidate","candidate:1 1 UDP 1 127.0.0.1 50000 typ host"},{"mid","video"}});}
            if(mode==10) for(int n=0;n<65;++n) bad.Send({{"type","ice-candidate"},{"session",id},
                {"candidate","candidate:1 1 UDP 1 127.0.0.1 50000 typ host"},{"mid","video"}});
        }
        bad.Closed();Wait([&]{return server.counters.sessions==1;});
        CHECK(queue->Size()==1); // Bad viewer cannot clear the controller's input.
    }
    owner.Send(KeyMessage(true)); owner.Closed(); // Even the controller cannot use the old input path.
    Wait([&]{return server.counters.sessions==0;});CHECK(queue->TakeReleaseAll());
    Client reconnect;CHECK(reconnect.Connect(port));const auto next=reconnect.Until("welcome");CHECK(next.at("session")!=ownerId);
    reconnect.Send({{"type","hello"},{"session",ownerId}});reconnect.Closed();
    Client slow;CHECK(slow.Connect(port));const auto id=slow.Until("welcome").at("session");
    slow.Send({{"type","hello"},{"session",id}});slow.Until("offer");
    const auto start=std::chrono::steady_clock::now();server.Stop();
    CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));CHECK(server.counters.sessions==0);
}
#endif
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
    auto signal=boost::json::object{{"v",1},{"seq",1},{"type","hello"},{"session",std::string(32,'a')}};
    CHECK(!ParseWebMessage(boost::json::serialize(signal),config,state,result));
    config.transport=render_module::WebTransport::WebRtc;
    CHECK(ClampWebViewport(1,1,config).width==16 && ClampWebViewport(1,1,config).height==16);
    CHECK(ClampWebViewport(1279,719,config).width==1278 && ClampWebViewport(1279,719,config).height==718);
    CHECK(ParseWebMessage(boost::json::serialize(signal),config,state,result));
    signal["v"]=2;CHECK(!ParseWebMessage(boost::json::serialize(signal),config,state,result));signal["v"]=1;
    signal["type"]="offer";CHECK(!ParseWebMessage(boost::json::serialize(signal),config,state,result));
    signal["type"]="answer";signal["sdp"]=std::string(32769,'a');CHECK(!ParseWebMessage(boost::json::serialize(signal),config,state,result));
    signal["sdp"]="valid-size";CHECK(ParseWebMessage(boost::json::serialize(signal),config,state,result));
    signal["session"]=std::string(32,'G');CHECK(!ParseWebMessage(boost::json::serialize(signal),config,state,result));
    EncodedImage encoded;encoded.codec=ImageCodec::Jpeg;encoded.frameId=0x0102030405060708ULL;
    encoded.width=640;encoded.height=480;encoded.bytes={1,2,3};auto packet=PackImage(encoded);
    CHECK(packet.size()==31 && std::memcmp(packet.data(),"RMIM",4)==0 && packet[5]==2 && packet[7]==1 &&
          packet[8]==1 && packet[15]==8 && packet[24]==0 && packet[27]==3);
    encoded.codec=ImageCodec::Png;packet=PackImage(encoded);CHECK(packet[7]==2);
    CHECK(std::string(ImageCodecName(ImageCodec::Png))=="png" && std::string(ImageCodecMime(ImageCodec::Png))=="image/png");
    encoded.codec=static_cast<ImageCodec>(99);CHECK(PackImage(encoded).empty());
    encoded.codec=ImageCodec::Jpeg;encoded.frameId=0;CHECK(PackImage(encoded).empty());
    encoded.frameId=1;encoded.bytes.resize(WebFrameLimit+1);CHECK(PackImage(encoded).empty());
}
void PngTests() {
    ImageRgba pattern; pattern.width=67;pattern.height=53;pattern.pixels.resize(std::size_t(pattern.width)*pattern.height*4);
    for(int y=0;y<pattern.height;++y) for(int x=0;x<pattern.width;++x) {
        auto* p=&pattern.pixels[(std::size_t(y)*pattern.width+x)*4];
        p[0]=static_cast<unsigned char>((x*255)/(pattern.width-1));
        p[1]=static_cast<unsigned char>((y*255)/(pattern.height-1));
        p[2]=static_cast<unsigned char>(((x^y)&1)?255:0);
        p[3]=static_cast<unsigned char>((x+y)%5?255:(x*3+y*5)&255);
        if(x<8&&y<8) { const unsigned char colors[][4]={{0,0,0,0},{255,255,255,255},{255,0,0,128},{0,255,0,64},{0,0,255,255}};
            std::memcpy(p,colors[(x+y)%5],4); }
    }
    ImageEncoder encoder(render_module::WebSocketImageCodec::Png,80,WebFrameLimit);EncodedImage encoded;
    for(std::uint64_t id=1;id<=20;++id) {
        CHECK(encoder.Encode(pattern,id,encoded));CHECK(encoded.codec==ImageCodec::Png && encoded.frameId==id);
        const unsigned char signature[]={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
        CHECK(encoded.bytes.size()>32 && std::memcmp(encoded.bytes.data(),signature,8)==0);
        int w=0,h=0,channels=0;auto* decoded=stbi_load_from_memory(encoded.bytes.data(),int(encoded.bytes.size()),&w,&h,&channels,4);
        CHECK(decoded && w==pattern.width && h==pattern.height &&
              std::memcmp(decoded,pattern.pixels.data(),pattern.pixels.size())==0);
        stbi_image_free(decoded);
    }
    // stb emits only signature/IHDR/IDAT/IEND: no profile/gamma chunks that can transform samples.
    std::size_t offset=8;std::vector<std::string> chunks;
    while(offset+12<=encoded.bytes.size()) {
        const auto length=(std::uint32_t(encoded.bytes[offset])<<24)|(std::uint32_t(encoded.bytes[offset+1])<<16)|
            (std::uint32_t(encoded.bytes[offset+2])<<8)|encoded.bytes[offset+3];
        CHECK(offset+12+length<=encoded.bytes.size());chunks.emplace_back(reinterpret_cast<const char*>(encoded.bytes.data()+offset+4),4);
        offset+=12+length;
    }
    CHECK(chunks.size()==3 && chunks[0]=="IHDR" && chunks[1]=="IDAT" && chunks[2]=="IEND");
    ImageRgba invalid;std::vector<unsigned char> bytes{1};CHECK(!ImagePresenter::EncodePng(invalid,bytes));
    ImageEncoder tiny(render_module::WebSocketImageCodec::Png,80,8);CHECK(!tiny.Encode(pattern,1,encoded));
    ImageRgba noise;noise.width=1920;noise.height=1080;noise.pixels.resize(std::size_t(noise.width)*noise.height*4);
    std::uint32_t random=1;for(auto& byte:noise.pixels){random=random*1664525u+1013904223u;byte=static_cast<unsigned char>(random>>24);}
    CHECK(encoder.Encode(noise,22,encoded) && encoded.bytes.size()<WebFrameLimit);
    noise.width=noise.height=2048;noise.pixels.resize(std::size_t(noise.width)*noise.height*4);
    for(auto& byte:noise.pixels){random=random*1664525u+1013904223u;byte=static_cast<unsigned char>(random>>24);}
    CHECK(!encoder.Encode(noise,23,encoded));
    // Actual root readback uses the same sole flip as screenshots and must round-trip exactly.
    render_module::Config config;config.backend=render_module::Backend::Headless;config.headlessContext=render_module::HeadlessContext::NativeEgl;
    config.width=128;config.height=96;CHECK(RenderModule::Init(config));RootFramebuffer root;CHECK(root.Resize(64,48));root.BeginFrame();
    glDisable(GL_SCISSOR_TEST);glClearColor(0,0,1,.25f);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,24,64,24);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    const auto now=std::chrono::steady_clock::now();const auto frame=root.Complete(1,now,now);ImageRgba actual;CHECK(ImagePresenter::Read(frame,actual));
    CHECK(encoder.Encode(actual,21,encoded));int w=0,h=0,channels=0;
    auto* decoded=stbi_load_from_memory(encoded.bytes.data(),int(encoded.bytes.size()),&w,&h,&channels,4);
    CHECK(decoded && w==64 && h==48 && std::memcmp(decoded,actual.pixels.data(),actual.pixels.size())==0);
    CHECK(decoded[(5*64+32)*4]==255 && decoded[(42*64+32)*4+2]==255);stbi_image_free(decoded);
    root.Destroy();RenderModule::Shutdown();
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
        else if (std::strcmp(argv[1], "protocol") == 0) ProtocolTests();
#ifdef WEBRTC_TEST
        else if (std::strcmp(argv[1], "signaling") == 0) SignalingTests();
#endif
        else if(std::strcmp(argv[1],"png")==0) PngTests();
        else JpegTests();
        std::cout << "Web tests passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; RenderModule::Shutdown(); return 1; }
}
