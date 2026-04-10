#pragma once
#include <QString>
#include <functional>

enum class GutterIconType {
    Error,          // red dot ●
    Warning,        // yellow triangle ▲
    Info,           // blue circle ℹ
    Tip,            // lightbulb 💡
    Breakpoint,     // red circle
};

struct GutterIconInfo {
    GutterIconType type;
    QString        tooltip;
    std::function<void()> clickHandler;
};
