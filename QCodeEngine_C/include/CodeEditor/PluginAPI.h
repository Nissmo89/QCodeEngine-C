#pragma once

#include <QString>

class CodeEditor;
class QKeyEvent;

class CodeEditorPlugin
{
public:
    virtual ~CodeEditorPlugin() = default;

    virtual QString id() const = 0;
    virtual void onAttach(CodeEditor* editor) = 0;
    virtual void onDetach(CodeEditor* editor) = 0;

    // Return true to consume the key event before the editor handles it.
    virtual bool onKeyPress(CodeEditor* /*editor*/, QKeyEvent* /*event*/) { return false; }
};

