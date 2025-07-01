#ifndef DEBUG_CONSOLE_HPP_
#define DEBUG_CONSOLE_HPP_

#include <iostream>
#include <string>
#include <streambuf>

#include "render_module/render_module.hpp"

class DebugConsole
{
public:
    // Nested ImGuiStreamBuf class
    class ImGuiStreamBuf : public std::streambuf {
    public:
        ImGuiStreamBuf(DebugConsole& console) : console(console) {}

    protected:
        int overflow(int c) override {
            if (c != EOF) {
                buffer += static_cast<char>(c);
                if (c == '\n') {
                    flushBuffer();
                }
            }
            return c;
        }

        int sync() override {
            flushBuffer();
            return 0;
        }

    private:
        void flushBuffer() {
            if (!buffer.empty()) {
                console.Log("%s", buffer.c_str());
                buffer.clear();
            }
        }

        std::string buffer;
        DebugConsole& console;
    };


    /* Accessor to the global instance */
    static DebugConsole& Console()
    {
        static DebugConsole instance;
        return instance;
    }

    void Log(const char* fmt, ...) IM_FMTARGS(2)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
        va_end(args);
        buf[IM_ARRAYSIZE(buf) - 1] = 0;
        std::string log_line = std::string("> ") + buf;
        logs.emplace_back(log_line);
        scrollToBottom = true;
    }

    void Clear()
    {
        logs.clear();
    }

    void Render(const char* title = "Debug Console", bool* p_open = nullptr)
    {
        static bool wordWrap = true;

        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        // ImGui::Text("FPS: %.2f", RenderModule::GetFPS());
        // ImGui::Separator();

        if (ImGui::Button("Clear")) Clear();
        ImGui::SameLine();
        if (ImGui::Button(wordWrap ? "Wrap: ON" : "Wrap: OFF"))
            wordWrap = !wordWrap;
        ImGui::Separator();

        ImVec2 child_size = ImVec2(0, 0);
        ImGui::BeginChild("ScrollingRegion", child_size, false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        if (wordWrap)
        {
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

    void SetCoutRedirect(bool enable)
    {
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

private:
    std::vector<std::string> logs;
    bool scrollToBottom = false;
};

#endif // DEBUG_CONSOLE_HPP_