#include "render_module/debug_console.hpp" 
#include "render_module/render_module.hpp"
#include <imgui.h>
#include <cstdarg>
#include <cstdio>

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

void DebugConsole::Log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = 0;
    std::string log_line = std::string("> ") + buf;
    logs.emplace_back(log_line);
    scrollToBottom = true;
}

void DebugConsole::Clear() {
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

    for (const auto& line : logs)
        ImGui::TextUnformatted(line.c_str());

    if (wordWrap)
        ImGui::PopTextWrapPos();

    if (scrollToBottom)
        ImGui::SetScrollHereY(1.0f);

    scrollToBottom = false;
    ImGui::EndChild();
    ImGui::End();
}

void DebugConsole::SetCoutRedirect(bool enable) {
    static std::streambuf* oldCoutBuf = nullptr;
    static ImGuiStreamBuf* imguiBuf = nullptr;

    if (enable) {
        if (!imguiBuf)
            imguiBuf = new ImGuiStreamBuf(*this);
        if (!oldCoutBuf)
            oldCoutBuf = std::cout.rdbuf();
        std::cout.rdbuf(imguiBuf);
    } else {
        if (oldCoutBuf)
            std::cout.rdbuf(oldCoutBuf);
    }
}
