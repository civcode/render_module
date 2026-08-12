#include "render_module/debug_console.hpp" 
#include "render_module/render_module.hpp"
#include <imgui.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <utility>

DebugConsole::ImGuiStreamBuf::ImGuiStreamBuf(DebugConsole& console) : console(console) {}

int DebugConsole::ImGuiStreamBuf::overflow(int c) {
    if (c != EOF) {
        buffer += static_cast<char>(c);
        if (c == '\n') {
            flushBuffer();
        }
    }
    return c;
}

int DebugConsole::ImGuiStreamBuf::sync() {
    flushBuffer();
    return 0;
}

void DebugConsole::ImGuiStreamBuf::flushBuffer() {
    if (!buffer.empty()) {
        console.Log("%s", buffer.c_str());
        buffer.clear();
    }
}

//
// DebugConsole Implementation
//

DebugConsole& DebugConsole::Console() {
    static DebugConsole instance;
    return instance;
}

DebugConsole::~DebugConsole() {
    SetCoutRedirect(false);
}

void DebugConsole::Log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;
    std::string log_line = std::string("> ") + buf;
    std::lock_guard<std::mutex> lock(mutex_);
    logs.emplace_back(std::move(log_line));
    if (maxEntries_ > 0 && logs.size() > maxEntries_) {
        const auto eraseCount = logs.size() - maxEntries_;
        logs.erase(logs.begin(), logs.begin() + static_cast<std::ptrdiff_t>(eraseCount));
    }
    scrollToBottom = true;
}

void DebugConsole::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    logs.clear();
}

void DebugConsole::Render(const char* title, bool* p_open) {
    static bool wordWrap = true;

    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return;
    }

    // Optional FPS display
    ImGui::Text("FPS: %.2f", RenderModule::GetFPS());
    ImGui::Separator();

    if (ImGui::Button("Clear")) Clear();
    ImGui::SameLine();
    if (ImGui::Button(wordWrap ? "Wrap: ON" : "Wrap: OFF"))
        wordWrap = !wordWrap;
    ImGui::Separator();

    ImVec2 child_size = ImVec2(0, 0);
    ImGui::BeginChild("ScrollingRegion", child_size, false, ImGuiWindowFlags_HorizontalScrollbar);

    if (wordWrap) {
        float wrap_width = ImGui::GetContentRegionAvail().x;
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);
    }

    std::vector<std::string> snapshot;
    bool shouldScroll = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = logs;
        shouldScroll = scrollToBottom;
        scrollToBottom = false;
    }

    for (const auto& line : snapshot)
        ImGui::TextUnformatted(line.c_str());

    if (wordWrap)
        ImGui::PopTextWrapPos();

    if (shouldScroll)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void DebugConsole::SetCoutRedirect(bool enable) {
    if (enable) {
        if (!imguiBuf_) imguiBuf_ = std::make_unique<ImGuiStreamBuf>(*this);
        if (!oldCoutBuf_) oldCoutBuf_ = std::cout.rdbuf();
        std::cout.rdbuf(imguiBuf_.get());
    } else {
        if (oldCoutBuf_ && std::cout.rdbuf() == imguiBuf_.get()) {
            std::cout.rdbuf(oldCoutBuf_);
        }
        oldCoutBuf_ = nullptr;
    }
}

void DebugConsole::SetMaxEntries(std::size_t maxEntries) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxEntries_ = maxEntries;
    if (maxEntries_ > 0 && logs.size() > maxEntries_) {
        const auto eraseCount = logs.size() - maxEntries_;
        logs.erase(logs.begin(), logs.begin() + static_cast<std::ptrdiff_t>(eraseCount));
    }
}
