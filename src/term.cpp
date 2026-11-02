#include "term.hpp"

#include "utf8.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace inkwell {
namespace {

termios g_original{};
bool g_have_original = false;
volatile sig_atomic_t g_resized = 0;

void restore_terminal() {
    const char sequence[] = "\033[?2004l\033[?25h\033[0m\033[?1049l";
    const ssize_t written = ::write(STDOUT_FILENO, sequence, sizeof(sequence) - 1);
    (void)written;
    if (g_have_original) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original);
        g_have_original = false;
    }
}

void on_stop(int signal_number) {
    restore_terminal();
    std::signal(signal_number, SIG_DFL);
    raise(signal_number);
}

void on_resize(int) {
    g_resized = 1;
}

}  // namespace

Terminal::~Terminal() {
    leave();
}

bool Terminal::enter(std::string& error) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        error = "Inkwell needs a terminal. Use --check to review a file.";
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &g_original) != 0) {
        error = "Could not read the terminal settings";
        return false;
    }
    termios raw = g_original;
    cfmakeraw(&raw);
    raw.c_lflag |= ISIG;
    raw.c_cc[VINTR] = 3;
    raw.c_cc[VSUSP] = _POSIX_VDISABLE;
    raw.c_cc[VQUIT] = _POSIX_VDISABLE;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        error = "Could not set the terminal to raw mode";
        return false;
    }
    g_have_original = true;
    raw_ = true;
    const char sequence[] = "\033[?1049h\033[?2004h\033[2J\033[H\033[?25h";
    write(sequence);
    update_size();
    std::signal(SIGINT, on_stop);
    std::signal(SIGTERM, on_stop);
    std::signal(SIGWINCH, on_resize);
    return true;
}

void Terminal::leave() {
    if (!raw_ && !g_have_original) {
        return;
    }
    raw_ = false;
    restore_terminal();
}

void Terminal::update_size() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0) {
        cols = size.ws_col;
        rows = size.ws_row;
    }
}

void Terminal::write(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::write(STDOUT_FILENO, data.data() + sent, data.size() - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        sent += static_cast<size_t>(n);
    }
}

int Terminal::read_byte(int timeout_ms, unsigned char& out) {
    while (true) {
        if (g_resized) {
            return -2;
        }
        pollfd waiter{};
        waiter.fd = STDIN_FILENO;
        waiter.events = POLLIN;
        const int ready = poll(&waiter, 1, timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                if (g_resized) {
                    return -2;
                }
                continue;
            }
            return -1;
        }
        if (ready == 0) {
            return 0;
        }
        const ssize_t n = ::read(STDIN_FILENO, &out, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        return 1;
    }
}

Key Terminal::read_paste() {
    std::string pasted;
    while (pasted.size() < 1000000) {
        unsigned char byte = 0;
        const int got = read_byte(1000, byte);
        if (got <= 0) {
            break;
        }
        pasted.push_back(static_cast<char>(byte));
        if (pasted.size() >= 6 && pasted.compare(pasted.size() - 6, 6, "\033[201~") == 0) {
            pasted.resize(pasted.size() - 6);
            break;
        }
    }
    std::string clean;
    clean.reserve(pasted.size());
    for (size_t i = 0; i < pasted.size(); ++i) {
        if (pasted[i] == '\r') {
            clean.push_back('\n');
            if (i + 1 < pasted.size() && pasted[i + 1] == '\n') {
                ++i;
            }
        } else {
            clean.push_back(pasted[i]);
        }
    }
    Key key;
    key.kind = KeyKind::Paste;
    key.text = std::move(clean);
    return key;
}

Key Terminal::read_escape(int timeout_ms) {
    unsigned char byte = 0;
    const int got = read_byte(timeout_ms, byte);
    if (got == -2) {
        return Key{KeyKind::Resize, 0, 0, false, {}};
    }
    if (got <= 0) {
        return Key{KeyKind::Escape, 0, 0, false, {}};
    }
    if (byte == 'O') {
        unsigned char final_byte = 0;
        if (read_byte(timeout_ms, final_byte) <= 0) {
            return Key{KeyKind::Escape, 0, 0, false, {}};
        }
        Key key;
        switch (final_byte) {
            case 'A':
                key.kind = KeyKind::Up;
                break;
            case 'B':
                key.kind = KeyKind::Down;
                break;
            case 'C':
                key.kind = KeyKind::Right;
                break;
            case 'D':
                key.kind = KeyKind::Left;
                break;
            case 'H':
                key.kind = KeyKind::Home;
                break;
            case 'F':
                key.kind = KeyKind::End;
                break;
            case 'P':
                key.kind = KeyKind::Function;
                key.code = 1;
                break;
            case 'Q':
                key.kind = KeyKind::Function;
                key.code = 2;
                break;
            case 'R':
                key.kind = KeyKind::Function;
                key.code = 3;
                break;
            case 'S':
                key.kind = KeyKind::Function;
                key.code = 4;
                break;
            default:
                key.kind = KeyKind::Escape;
                break;
        }
        return key;
    }
    if (byte != '[') {
        Key key;
        key.kind = KeyKind::Rune;
        key.rune = byte;
        return key;
    }

    std::string sequence;
    unsigned char final_byte = 0;
    while (sequence.size() < 32) {
        unsigned char next = 0;
        const int more = read_byte(timeout_ms, next);
        if (more == -2) {
            return Key{KeyKind::Resize, 0, 0, false, {}};
        }
        if (more <= 0) {
            return Key{KeyKind::Escape, 0, 0, false, {}};
        }
        sequence.push_back(static_cast<char>(next));
        if (next >= 0x40 && next <= 0x7E) {
            final_byte = next;
            break;
        }
    }
    std::vector<int> params;
    int current = -1;
    const std::string numbers = sequence.empty() ? std::string{} : sequence.substr(0, sequence.size() - 1);
    for (char c : numbers) {
        if (c >= '0' && c <= '9') {
            if (current < 0) {
                current = 0;
            }
            current = current * 10 + (c - '0');
        } else if (c == ';') {
            params.push_back(current < 0 ? 0 : current);
            current = -1;
        }
    }
    if (current >= 0) {
        params.push_back(current);
    }

    bool ctrl = false;
    if (params.size() >= 2) {
        const int modifier = params.back();
        ctrl = modifier == 5 || modifier == 6 || modifier == 7;
    }
    const int primary = params.empty() ? 0 : params.front();
    Key key;
    key.ctrl = ctrl;
    if (final_byte == '~') {
        if (primary == 200) {
            return read_paste();
        }
        switch (primary) {
            case 1:
            case 7:
                key.kind = KeyKind::Home;
                break;
            case 4:
            case 8:
                key.kind = KeyKind::End;
                break;
            case 3:
                key.kind = KeyKind::Delete;
                break;
            case 5:
                key.kind = KeyKind::PageUp;
                break;
            case 6:
                key.kind = KeyKind::PageDown;
                break;
            case 11:
                key.kind = KeyKind::Function;
                key.code = 1;
                break;
            case 12:
                key.kind = KeyKind::Function;
                key.code = 2;
                break;
            case 13:
                key.kind = KeyKind::Function;
                key.code = 3;
                break;
            case 14:
                key.kind = KeyKind::Function;
                key.code = 4;
                break;
            case 15:
                key.kind = KeyKind::Function;
                key.code = 5;
                break;
            case 17:
                key.kind = KeyKind::Function;
                key.code = 6;
                break;
            case 18:
                key.kind = KeyKind::Function;
                key.code = 7;
                break;
            case 19:
                key.kind = KeyKind::Function;
                key.code = 8;
                break;
            case 20:
                key.kind = KeyKind::Function;
                key.code = 9;
                break;
            case 21:
                key.kind = KeyKind::Function;
                key.code = 10;
                break;
            case 23:
                key.kind = KeyKind::Function;
                key.code = 11;
                break;
            case 24:
                key.kind = KeyKind::Function;
                key.code = 12;
                break;
            default:
                key.kind = KeyKind::Escape;
                break;
        }
        return key;
    }
    switch (final_byte) {
        case 'A':
            key.kind = KeyKind::Up;
            break;
        case 'B':
            key.kind = KeyKind::Down;
            break;
        case 'C':
            key.kind = KeyKind::Right;
            break;
        case 'D':
            key.kind = KeyKind::Left;
            break;
        case 'H':
            key.kind = KeyKind::Home;
            break;
        case 'F':
            key.kind = KeyKind::End;
            break;
        default:
            key.kind = KeyKind::Escape;
            break;
    }
    return key;
}

Key Terminal::read_key(int timeout_ms) {
    if (g_resized) {
        g_resized = 0;
        update_size();
        return Key{KeyKind::Resize, 0, 0, false, {}};
    }
    unsigned char byte = 0;
    const int got = read_byte(timeout_ms, byte);
    if (got == -2) {
        g_resized = 0;
        update_size();
        return Key{KeyKind::Resize, 0, 0, false, {}};
    }
    if (got <= 0) {
        return got == 0 ? Key{KeyKind::None, 0, 0, false, {}} : Key{KeyKind::Eof, 0, 0, false, {}};
    }
    if (byte == 0) {
        return Key{KeyKind::Mark, 0, 0, false, {}};
    }
    if (byte == '\r' || byte == '\n') {
        return Key{KeyKind::Enter, 0, 0, false, {}};
    }
    if (byte == 127 || byte == 8) {
        return Key{KeyKind::Backspace, 0, 0, false, {}};
    }
    if (byte == '\t') {
        return Key{KeyKind::Tab, 0, 0, false, {}};
    }
    if (byte == 27) {
        return read_escape(30);
    }
    if (byte < 32) {
        Key key;
        key.kind = KeyKind::Ctrl;
        key.code = static_cast<int>('a' + byte - 1);
        return key;
    }
    if (byte < 0x80) {
        Key key;
        key.kind = KeyKind::Rune;
        key.rune = byte;
        return key;
    }
    const int length = utf8_seq_len(byte);
    std::string encoded(1, static_cast<char>(byte));
    for (int i = 1; i < length; ++i) {
        unsigned char next = 0;
        if (read_byte(30, next) <= 0) {
            break;
        }
        encoded.push_back(static_cast<char>(next));
    }
    int seq = 1;
    const char32_t rune = utf8_decode(encoded, 0, seq);
    Key key;
    key.kind = KeyKind::Rune;
    key.rune = rune == 0 ? static_cast<char32_t>(byte) : rune;
    return key;
}

}  // namespace inkwell
