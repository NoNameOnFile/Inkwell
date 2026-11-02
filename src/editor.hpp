#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace inkwell {

class Editor {
public:
    Editor();

    void insert(std::string text);
    void newline();
    void backspace();
    void delete_forward();
    void move_left();
    void move_right();
    void move_home();
    void move_end();
    void move_word_left();
    void move_word_right();
    void move_document_start();
    void move_document_end();
    void set_cursor(int next_line, int next_col);
    void toggle_mark();
    void clear_mark();

    void undo();
    void redo();

    bool load_file(const std::string& file, std::string& error);
    bool save_file(const std::string& file, std::string& error);

    void replace_span(size_t begin, size_t end, const std::string& replacement);

    bool has_selection() const;
    std::pair<size_t, size_t> selection_bounds() const;
    std::string selection_text() const;

    std::string text() const;
    size_t offset_of(int at_line, int at_col) const;
    std::pair<int, int> cursor_at(size_t offset) const;
    size_t cursor_offset() const;

    int cursor_line() const { return line_; }
    int cursor_col() const { return col_; }
    const std::vector<std::string>& lines() const { return lines_; }
    bool dirty() const { return dirty_; }
    const std::string& path() const { return path_; }
    void set_path(std::string file) { path_ = std::move(file); }
    bool mark_active() const { return mark_on_; }

private:
    struct Snapshot {
        std::vector<std::string> lines;
        int line = 0;
        int col = 0;
        bool mark_on = false;
        int mark_line = 0;
        int mark_col = 0;
    };

    enum class Action { None, Type, Delete, Other };

    void assign_text(std::string text);
    void clamp_cursor();
    void erase_selection();
    void push_undo();
    void before_type(bool coalesce);
    void before_delete();
    void before_other();
    Snapshot capture() const;
    void restore(const Snapshot& snapshot);
    void note_saved();

    std::vector<std::string> lines_{{""}};
    int line_ = 0;
    int col_ = 0;
    bool mark_on_ = false;
    int mark_line_ = 0;
    int mark_col_ = 0;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    Action last_action_ = Action::None;
    bool dirty_ = false;
    std::string path_;
    std::string saved_body_;
};

}  // namespace inkwell
