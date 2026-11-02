#pragma once

#include <string>

namespace inkwell {

enum class KeyKind {
    None,
    Eof,
    Resize,
    Rune,
    Enter,
    Backspace,
    Delete,
    Tab,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Function,
    Ctrl,
    Mark,
    Paste,
};

struct Key {
    KeyKind kind = KeyKind::None;
    char32_t rune = 0;
    int code = 0;
    bool ctrl = false;
    std::string text;
};

class Terminal {
public:
    Terminal() = default;
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    bool enter(std::string& error);
    void leave();
    void update_size();
    Key read_key(int timeout_ms);
    void write(const std::string& data);

    int cols = 80;
    int rows = 24;

private:
    int read_byte(int timeout_ms, unsigned char& out);
    Key read_escape(int timeout_ms);
    Key read_paste();
    bool raw_ = false;
};

}  // namespace inkwell
