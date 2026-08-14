#ifndef RENDER_CONTEXT_HPP_
#define RENDER_CONTEXT_HPP_

struct RenderContext {
    // bool disableViewportDrag = false;
    bool disableViewportControls = false;

    static RenderContext& Instance() {
        static RenderContext instance;
        return instance;
    }
};

struct ScopedViewPortLock {
    ScopedViewPortLock() : previous_(RenderContext::Instance().disableViewportControls) {
        RenderContext::Instance().disableViewportControls = true;
    }

    ~ScopedViewPortLock() {
        RenderContext::Instance().disableViewportControls = previous_;
    }

    ScopedViewPortLock(const ScopedViewPortLock&) = delete;
    ScopedViewPortLock& operator=(const ScopedViewPortLock&) = delete;

private:
    bool previous_;
};

#endif // RENDER_STATE_HPP_
