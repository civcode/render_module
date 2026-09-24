#include "input/remote_input_backend.hpp"
#include <imgui_internal.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace render_module::detail;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while (false)
std::size_t Id(RenderKey key) { return static_cast<std::size_t>(key); }

void QueueTests() {
    CHECK(!ImGui::GetCurrentContext());
    RemoteInputQueue queue;
    CHECK(queue.Enqueue(MouseMove{0, 0}).Accepted());
    CHECK(queue.Enqueue(MouseMove{1, 1}).status == EnqueueStatus::Coalesced);
    CHECK(queue.Size() == 1);
    CHECK(queue.Enqueue(MouseButton{RemoteMouseButton::Left, true}).Accepted());
    CHECK(queue.Enqueue(MouseMove{.25f, .75f}).Accepted());
    QueuedInput item;
    CHECK(queue.TryPop(item) && item.sequence == 2);
    CHECK(std::get<MouseMove>(item.event).nx == 1);
    CHECK(queue.TryPop(item) && std::holds_alternative<MouseButton>(item.event));
    CHECK(queue.TryPop(item) && std::get<MouseMove>(item.event).nx == .25f);
    CHECK(!queue.TryPop(item));
    std::vector<RemoteInputEvent> reliable{Key{RenderKey::A, true}, TextUtf8{"hello"},
        Focus{true}, MouseButton{RemoteMouseButton::Right, false}, ReleaseAll{}, InputStateSnapshot{}};
    for (const auto& event : reliable) CHECK(queue.Enqueue(event).Accepted());
    std::uint64_t last = 0;
    for (const auto& event : reliable) {
        CHECK(queue.TryPop(item) && item.event.index() == event.index());
        CHECK(item.sequence > last); last = item.sequence;
    }
    for (std::size_t i = 0; i < queue.Capacity; ++i) CHECK(queue.Enqueue(Key{RenderKey::A, true}).Accepted());
    CHECK(queue.Size() == queue.Capacity);
    CHECK(queue.Enqueue(Key{RenderKey::A, false}, 99).status == EnqueueStatus::Full);
    CHECK(queue.Enqueue(MouseButton{RemoteMouseButton::Left, false}).status == EnqueueStatus::Full);
    CHECK(queue.Size() == queue.Capacity); // No accepted reliable event was evicted.
    CHECK(queue.TryPop(item));
    CHECK(queue.Enqueue(Key{RenderKey::A, false}, 99).Accepted()); // Retry isn't stale.
    CHECK(queue.Enqueue(Key{RenderKey::A, true}, 98).status == EnqueueStatus::Stale);
    CHECK(queue.ReleaseAllInput()); // Available despite saturation.
    CHECK(queue.Size() == 0);
    CHECK(queue.Enqueue(Focus{true}, 100).Accepted());
    CHECK(!queue.TryPop(item)); // Cannot cross the cancellation barrier.
    CHECK(queue.TakeReleaseAll());
    CHECK(!queue.TakeReleaseAll());
    CHECK(queue.TryPop(item) && std::holds_alternative<Focus>(item.event));
    CHECK(queue.Enqueue(InputStateSnapshot{}, 101).Accepted());
    CHECK(queue.Enqueue(MouseMove{0, 0}, 100).status == EnqueueStatus::Stale);
    CHECK(queue.TryPop(item));
    CHECK(queue.Enqueue(MouseMove{-2, 8}).Accepted());
    CHECK(queue.TryPop(item));
    CHECK(std::get<MouseMove>(item.event).nx == 0 && std::get<MouseMove>(item.event).ny == 1);
    for (float bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(queue.Enqueue(MouseMove{bad, 0}).status == EnqueueStatus::Invalid);
        CHECK(queue.Enqueue(MouseMove{0, bad}).status == EnqueueStatus::Invalid);
        CHECK(queue.Enqueue(MouseWheel{bad, 1}).status == EnqueueStatus::Invalid);
        InputStateSnapshot snapshot; snapshot.ny = bad;
        CHECK(queue.Enqueue(snapshot).status == EnqueueStatus::Invalid);
    }
    CHECK(queue.Enqueue(Key{RenderKey::Count, true}).status == EnqueueStatus::Invalid);
    CHECK(queue.Enqueue(MouseButton{RemoteMouseButton::Count, true}).status == EnqueueStatus::Invalid);
    CHECK(queue.Enqueue(MouseSource{static_cast<RemoteMouseSource>(99)}).status == EnqueueStatus::Invalid);
    for (auto invalid : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
                         std::string("\xc3"), std::string("a\0b", 3), std::string(257, 'a')})
        CHECK(queue.Enqueue(TextUtf8{invalid}).status == EnqueueStatus::Invalid);
    CHECK(queue.Enqueue(TextUtf8{std::string(256, 'a')}).Accepted());
    queue.Close();
    CHECK(!queue.ReleaseAllInput());
    CHECK(queue.Enqueue(Focus{true}).status == EnqueueStatus::Closed);
    CHECK(!queue.TryPop(item));

    // Contended producers and a concurrent consumer: accepted order is mutex
    // acquisition order, monotonic globally and FIFO within every producer.
    RemoteInputQueue concurrent;
    std::atomic<bool> failed{false};
    std::vector<std::thread> producers;
    constexpr int ProducerCount = 4, PerProducer = 1000;
    for (int p = 0; p < ProducerCount; ++p) producers.emplace_back([&, p] {
        for (int n = 0; n < PerProducer; ++n) {
            const TextUtf8 event{std::to_string(p) + ":" + std::to_string(n)};
            while (true) {
                const auto result = concurrent.Enqueue(event);
                if (result.Accepted()) break;
                if (result.status != EnqueueStatus::Full) { failed = true; return; }
                std::this_thread::yield();
            }
        }
    });
    std::array<int, ProducerCount> counts{};
    last = 0;
    for (int n = 0; n < ProducerCount*PerProducer;) {
        if (!concurrent.TryPop(item)) { std::this_thread::yield(); continue; }
        const auto& text = std::get<TextUtf8>(item.event);
        int producer = -1, index = -1;
        if (std::sscanf(text.bytes.data(), "%d:%d", &producer, &index) != 2 ||
            producer < 0 || producer >= ProducerCount) { failed = true; break; }
        if (index != counts[producer]++ || item.sequence <= last) failed = true;
        last = item.sequence; ++n;
    }
    concurrent.Close();
    for (auto& producer : producers) producer.join();
    CHECK(!failed);
    for (int count : counts) CHECK(count == PerProducer);
}

struct Fixture {
    RemoteInputBackend input;
    std::shared_ptr<RemoteInputQueue> queue;
    Fixture() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        unsigned char* pixels; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        CHECK(input.Init()); CHECK(input.Init());
        queue = input.EventQueue();
    }
    ~Fixture() { input.Shutdown(); ImGui::DestroyContext(); }
    void Send(RemoteInputEvent event) { CHECK(queue->Enqueue(std::move(event)).Accepted()); }
    void Frame(int width = 800, int height = 600, double dt = 1.0/60.0) {
        input.BeginFrame(ImGui::GetIO(), width, height, dt);
        ImGui::NewFrame();
    }
    void End() { ImGui::EndFrame(); }
    void Settle(int width = 800, int height = 600) {
        for (int i = 0; i < 1000; ++i) {
            Frame(width, height); End();
            if (!queue->Size() && ImGui::GetCurrentContext()->InputEventsQueue.empty()) return;
        }
        CHECK(false);
    }
};
void EventTests() {
    Fixture f;
    auto& io = ImGui::GetIO();
    CHECK(sizeof(ImWchar) == 4); // Four-byte UTF-8 must not become U+FFFD.
    f.Send(Focus{false}); f.Frame(); CHECK(io.AppFocusLost); f.End();
    f.Send(Focus{true}); f.Frame(); CHECK(!io.AppFocusLost); f.End();
    for (std::size_t i = 0; i < KeyCount; ++i) CHECK(MapRenderKey(static_cast<RenderKey>(i)) != ImGuiKey_None);
    CHECK(MapRenderKey(RenderKey::Count) == ImGuiKey_None);
    CHECK(MapRenderKey(RenderKey::A) == ImGuiKey_A && MapRenderKey(RenderKey::Z) == ImGuiKey_Z);
    CHECK(MapRenderKey(RenderKey::F12) == ImGuiKey_F12 && MapRenderKey(RenderKey::Down) == ImGuiKey_DownArrow);
    CHECK(MapRenderKey(RenderKey::Digit9) == ImGuiKey_9 && MapRenderKey(RenderKey::KeypadEnter) == ImGuiKey_KeypadEnter);
    CHECK(MapRenderKey(RenderKey::Semicolon) == ImGuiKey_Semicolon);
    for (auto position : {MouseMove{0, 0}, MouseMove{1, 1}, MouseMove{.25f, .75f}}) {
        f.Send(position); f.Frame();
        CHECK(io.MousePos.x == position.nx*800 && io.MousePos.y == position.ny*600);
        CHECK(io.DisplaySize.x == 800 && io.DisplayFramebufferScale.x == 1);
        f.End();
    }
    f.Frame(400, 200); // Stationary normalized cursor survives resize.
    CHECK(io.MousePos.x == 100 && io.MousePos.y == 150); f.End();
    f.Send(MouseMove{1, 1}); f.Frame(200, 100);
    CHECK(io.MousePos.x == 200 && io.MousePos.y == 100); f.End();
    f.Settle(100, 100);
    f.Send(MouseMove{.125f, .375f});
    f.Send(MouseButton{RemoteMouseButton::Left, true});
    f.Send(MouseButton{RemoteMouseButton::Left, false});
    f.Send(MouseMove{.875f, .5f});
    f.Frame(100, 100);
    CHECK(io.MouseDown[0] && io.MousePos.x == 12); f.End();
    // Pending release uses the reprojected original click anchor, not a future
    // move. Fractional normalized precision must survive ImGui's pixel flooring.
    f.Frame(1000, 500);
    CHECK(!io.MouseDown[0] && io.MousePos.x == 125 && io.MousePos.y == 187); f.End();
    f.Frame(1000, 500);
    CHECK(io.MousePos.x == 875 && io.MousePos.y == 250); f.End();
    f.Frame(800, 600, std::numeric_limits<double>::quiet_NaN());
    CHECK(std::isfinite(io.DeltaTime) && io.DeltaTime > 0); f.End();
    for (std::size_t i = 0; i < MouseButtonCount; ++i) {
        f.Send(MouseButton{static_cast<RemoteMouseButton>(i), true}); f.Settle();
        CHECK(io.MouseDown[i]);
        f.Send(MouseButton{static_cast<RemoteMouseButton>(i), false}); f.Settle();
        CHECK(!io.MouseDown[i]);
    }
    f.Send(MouseWheel{2, -3}); f.Frame();
    CHECK(io.MouseWheelH == 2 && io.MouseWheel == -3); f.End();
    for (auto source : {RemoteMouseSource::Mouse, RemoteMouseSource::Touch, RemoteMouseSource::Pen}) {
        // ImGui latches source onto actual pointer events and filters identical positions.
        f.Send(MouseSource{source}); f.Send(MouseMove{.2f + .1f*static_cast<int>(source), .5f}); f.Frame();
        CHECK(io.MouseSource == (source == RemoteMouseSource::Mouse ? ImGuiMouseSource_Mouse :
              source == RemoteMouseSource::Touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Pen));
        f.End();
    }
    for (auto key : {RenderKey::A, RenderKey::Enter, RenderKey::F1, RenderKey::F12, RenderKey::Left,
                     RenderKey::Up, RenderKey::Keypad7, RenderKey::Apostrophe}) {
        f.Send(Key{key, true}); f.Settle(); CHECK(ImGui::IsKeyDown(MapRenderKey(key)));
        f.Send(Key{key, false}); f.Settle(); CHECK(!ImGui::IsKeyDown(MapRenderKey(key)));
    }
    for (auto modifier : {RenderKey::LeftCtrl, RenderKey::RightShift, RenderKey::LeftAlt, RenderKey::RightSuper}) {
        f.Send(Key{modifier, true}); f.Send(Key{RenderKey::A, true}); f.Settle();
        CHECK(ImGui::IsKeyDown(ImGuiKey_A));
        CHECK(io.KeyCtrl == (modifier == RenderKey::LeftCtrl));
        CHECK(io.KeyShift == (modifier == RenderKey::RightShift));
        CHECK(io.KeyAlt == (modifier == RenderKey::LeftAlt));
        CHECK(io.KeySuper == (modifier == RenderKey::RightSuper));
        f.Send(Focus{false}); f.Settle();
        CHECK(!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper);
        CHECK(!ImGui::IsKeyDown(ImGuiKey_A));
        f.Send(Focus{true}); f.Settle();
    }
    f.Send(Key{RenderKey::LeftCtrl, true}); f.Send(Key{RenderKey::RightCtrl, true}); f.Settle();
    f.Send(Key{RenderKey::LeftCtrl, false}); f.Settle(); CHECK(io.KeyCtrl);
    f.Send(Key{RenderKey::RightCtrl, false}); f.Settle(); CHECK(!io.KeyCtrl);
    f.Send(TextUtf8{u8"ASCII äöüÄÖÜß é 日本 🙂"}); f.Frame();
    const unsigned int expected[] = {'A','S','C','I','I',' ',0xe4,0xf6,0xfc,0xc4,0xd6,0xdc,0xdf,' ',0xe9,' ',0x65e5,0x672c,' ',0x1f642};
    CHECK(io.InputQueueCharacters.Size == int(sizeof(expected)/sizeof(*expected)));
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) CHECK(io.InputQueueCharacters[i] == expected[i]);
    f.End();
    f.Send(Key{RenderKey::A, true}); f.Send(Key{RenderKey::LeftShift, true});
    f.Send(MouseButton{RemoteMouseButton::Right, true}); f.Settle();
    f.Send(Focus{false}); f.Send(Focus{true}); f.Settle(); // Cannot cancel defensive releases.
    CHECK(!ImGui::IsKeyDown(ImGuiKey_A) && !io.KeyShift && !io.MouseDown[1]);
    InputStateSnapshot snapshot;
    snapshot.nx = .75f; snapshot.ny = .25f;
    snapshot.keys.set(Id(RenderKey::B)); snapshot.ctrl = true; snapshot.mouseButtons.set(2);
    f.Send(MouseMove{0, 0}); f.Send(snapshot); f.Settle();
    CHECK(ImGui::IsKeyDown(ImGuiKey_B) && io.KeyCtrl && io.MouseDown[2]);
    CHECK(io.MousePos.x == 600 && io.MousePos.y == 150);
    snapshot.keys.reset(); snapshot.keys.set(Id(RenderKey::C)); snapshot.ctrl = false;
    snapshot.shift = true; snapshot.mouseButtons.reset(); snapshot.mouseButtons.set(0);
    f.Send(snapshot); f.Settle();
    CHECK(!ImGui::IsKeyDown(ImGuiKey_B) && ImGui::IsKeyDown(ImGuiKey_C));
    CHECK(!io.KeyCtrl && io.KeyShift && io.MouseDown[0] && !io.MouseDown[2]);
    snapshot.focused = false; f.Send(snapshot); f.Settle();
    CHECK(!io.KeyShift && !io.MouseDown[0] && !ImGui::IsKeyDown(ImGuiKey_C));
    f.Send(Key{RenderKey::D, true}); f.Send(TextUtf8{"ignored"}); f.Settle();
    CHECK(!ImGui::IsKeyDown(ImGuiKey_D));
    snapshot.focused = true; f.Send(snapshot); f.Settle();
    CHECK(io.KeyShift && io.MouseDown[0]);
    f.Send(ReleaseAll{false}); f.Settle();
    CHECK(!io.KeyShift && !io.MouseDown[0] && !ImGui::IsKeyDown(ImGuiKey_C));
    f.Send(Key{RenderKey::E, true});
    f.Send(Key{RenderKey::LeftCtrl, true});
    f.Send(MouseButton{RemoteMouseButton::Right, true});
    f.Settle(); CHECK(ImGui::IsKeyDown(ImGuiKey_E) && io.KeyCtrl && io.MouseDown[1]);
    // Fill both queues, then disconnect. Release-all bypasses both backlogs.
    for (std::size_t i = 0; i < RemoteInputQueue::Capacity; ++i) f.Send(Key{RenderKey::A, (i%2) == 0});
    f.Frame(); f.End();
    CHECK(ImGui::GetCurrentContext()->InputEventsQueue.Size > 0);
    CHECK(f.queue->ReleaseAllInput());
    f.Frame();
    CHECK(!ImGui::IsKeyDown(ImGuiKey_E) && !ImGui::IsKeyDown(ImGuiKey_A));
    CHECK(!io.KeyCtrl && !io.MouseDown[1] && io.AppFocusLost); f.End();
    CHECK(f.queue->Size() == 0);
    f.Send(Focus{true}); f.Settle();
    // Sustained transitions cannot leak into an ever-growing ImGui queue.
    for (int frame = 0; frame < 400; ++frame) {
        for (int n = 0; n < 64; ++n) {
            const auto result = f.queue->Enqueue(Key{RenderKey::F, (n%2) == 0});
            CHECK(result.Accepted() || result.status == EnqueueStatus::Full);
        }
        f.Frame();
        CHECK(ImGui::GetCurrentContext()->InputEventsQueue.Size < 512);
        CHECK(f.queue->Size() <= RemoteInputQueue::Capacity);
        f.End();
    }
    CHECK(f.queue->ReleaseAllInput()); f.Frame(); f.End();
    auto oldHandle = f.queue;
    std::atomic<bool> ready{false}, producerFailed{false};
    std::thread closingProducer([&] {
        while (true) {
            const auto result = oldHandle->Enqueue(Key{RenderKey::A, true});
            ready = true;
            if (result.status == EnqueueStatus::Closed) break;
            if (!result.Accepted() && result.status != EnqueueStatus::Full) producerFailed = true;
            std::this_thread::yield();
        }
    });
    while (!ready) std::this_thread::yield();
    f.input.Shutdown(); f.input.Shutdown();
    closingProducer.join(); CHECK(!producerFailed);
    CHECK(oldHandle->Enqueue(Focus{true}).status == EnqueueStatus::Closed);
    CHECK(f.input.Init()); CHECK(f.input.EventQueue() != oldHandle);
    f.queue = f.input.EventQueue();
    f.Send(Focus{true}); f.Send(Key{RenderKey::Z, true}); f.Settle();
    CHECK(ImGui::IsKeyDown(ImGuiKey_Z));
}
} // namespace
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        if (std::string(argv[1]) == "queue") QueueTests();
        else { CHECK(std::string(argv[1]) == "events"); EventTests(); }
        std::puts("Input tests passed."); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
