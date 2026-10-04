#pragma once
#include "Chain.h"

namespace fxblock {
struct EditorCallbacks {
    void* context = nullptr;
    double (*get)(void*, uint32_t) = nullptr;
    bool (*edit)(void*, uint32_t, double, int) = nullptr; // 0 value, 1 gesture begin, 2 gesture end
    void (*status)(void*, Status*) = nullptr;
};

class Editor {
public:
    virtual ~Editor() = default;
    virtual bool setParent(void* view) = 0;
    virtual void show(bool visible) = 0;
    virtual void* nativeView() const = 0;
    static Editor* create(EditorCallbacks callbacks);
    static constexpr uint32_t width = 960, height = 720;
};
} // namespace fxblock
