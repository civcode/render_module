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
    ScopedViewPortLock() {
        RenderContext::Instance().disableViewportControls = true;
    }

    ~ScopedViewPortLock() {
        RenderContext::Instance().disableViewportControls = false;    
    }
};

#endif // RENDER_STATE_HPP_