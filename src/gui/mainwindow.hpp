#pragma once

#include "book.hpp"
#include "details.hpp"
#include "pagesettings.hpp"
#include "theme.hpp"

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

class QAction;
class QCloseEvent;
class QEvent;
class QGridLayout;
class QLabel;
class QLayout;
class QLineEdit;
class QListWidget;
class QMenu;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

namespace inkwell {

class Dictionary;
class EditorView;
class ExportSheet;
class SideDrawer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);
    bool saveFile(const QString& path, bool quiet = false);
    void setDocumentText(const QString& text);
    QString documentText() const;
    int noteCount() const;
    QString noteSummary(int index) const;
    bool applyNote(int index);
    void recheck();
    void showDrawerPage(int index);
    void showLibraryPage(int index);
    void showExport();
    void setDarkMode(bool dark);
    void setTheme(ThemeId id);
    bool offerRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Note {
        int start = 0;
        int end = 0;
        int kind = 0;
        int line = 1;
        QString message;
        QStringList replacements;
    };

    void buildUi();
    void updateTitle();
    void updateCounts();
    void showNote(int index);
    void jumpToNote(int index);
    void stepNote(int direction);
    void learnWord(bool persist);
    void addName();
    void addNameFromNote();
    bool keepName(const QString& name);
    bool confirmDiscard();
    int noteAtCursor() const;
    void polishChrome();
    void applyTheme();
    void applyPageLook(bool restyle);
    void editPage();
    void commitLeaf();
    Leaf* activeLeaf();
    void loadActive();
    void showChapter(int index);
    void showStoryNote(int section, int index);
    void refreshLibrary();
    void addChapter();
    void importChapter();
    void addStoryNote(int section);
    void deleteActivePiece();
    bool saveBook(const QString& directory, bool quiet = false);
    bool loadBook(const QString& directory);
    bool singleFileBook() const;
    bool hasAutosaveTarget() const;
    void scheduleAutosave();
    void autosave();
    void finishSave(bool quiet, const QString& message);
    bool writeRecovery();
    void clearRecovery();
    void rememberRecent(const QString& path);
    void refreshRecentMenu();
    QStringList recentPaths() const;
    void saveCurrent();
    QString freshId();
    void insertPicture();
    void insertDataField();
    void clearLayout(QLayout* layout);
    void showShelf();
    void returnToShelf();
    void closeOpenBook();
    void showDesk();
    void showWriting();
    void refreshShelf();
    int shelfViewport() const;
    void newBook();
    void showCopyright();
    void showContents();
    void updateCommands();
    void chooseCover();
    void exportBook();
    void pullDetails();
    void pushDetails();
    void detailsEdited();
    QStringList chapterTitles() const;

    EditorView* editor_ = nullptr;
    SideDrawer* library_ = nullptr;
    SideDrawer* drawer_ = nullptr;
    QStackedWidget* root_stack_ = nullptr;
    QWidget* shelf_ = nullptr;
    QScrollArea* shelf_scroll_ = nullptr;
    QGridLayout* shelf_cards_ = nullptr;
    int shelf_columns_ = 0;
    int shelf_cover_width_ = 0;
    bool refreshing_shelf_ = false;
    QWidget* desk_ = nullptr;
    QStackedWidget* center_stack_ = nullptr;
    QWidget* matter_ = nullptr;
    ExportSheet* export_sheet_ = nullptr;
    EditorView* matter_view_ = nullptr;
    QLabel* matter_heading_ = nullptr;
    QWidget* canvas_ = nullptr;
    QLineEdit* title_edit_ = nullptr;
    QLabel* kicker_ = nullptr;
    QPushButton* delete_piece_ = nullptr;
    QVBoxLayout* chapter_rows_ = nullptr;
    QVBoxLayout* outline_rows_ = nullptr;
    QLabel* active_title_label_ = nullptr;
    QLabel* chapter_words_ = nullptr;
    Book book_;
    BookDetails details_;
    bool filling_details_ = false;
    QLineEdit* detail_title_ = nullptr;
    QLineEdit* detail_subtitle_ = nullptr;
    QLineEdit* detail_author_ = nullptr;
    QLineEdit* detail_edition_ = nullptr;
    QLineEdit* detail_year_ = nullptr;
    QLineEdit* detail_publisher_ = nullptr;
    QLineEdit* detail_epub_ = nullptr;
    QLineEdit* detail_paper_ = nullptr;
    QLineEdit* detail_pdf_ = nullptr;
    QLineEdit* detail_kindle_ = nullptr;
    QLineEdit* detail_hard_ = nullptr;
    QPlainTextEdit* detail_extra_ = nullptr;
    QCheckBox* detail_rights_ = nullptr;
    QCheckBox* detail_fiction_ = nullptr;
    QCheckBox* detail_moral_ = nullptr;
    QCheckBox* detail_external_ = nullptr;
    QCheckBox* detail_marks_ = nullptr;
    bool on_note_ = false;
    int active_chapter_ = 0;
    int active_section_ = 0;
    int active_note_ = -1;
    int leaf_seq_ = 1;
    QString book_dir_;
    bool book_dirty_ = false;
    qint64 dirty_at_ = 0;
    QLabel* suggestion_blurb_ = nullptr;
    QListWidget* notes_ = nullptr;
    QLabel* detail_ = nullptr;
    QLabel* counts_ = nullptr;
    QPushButton* apply_button_ = nullptr;
    QPushButton* name_button_ = nullptr;
    QVector<QPushButton*> quiet_buttons_;
    QTimer* debounce_ = nullptr;
    QTimer* autosave_ = nullptr;
    QVector<Note> items_;
    std::unique_ptr<Dictionary> dictionary_;
    QString path_;
    QString dict_error_;
    bool loading_ = false;
    PageSettings page_;
    ThemeId theme_id_ = ThemeId::Classic;
    bool dark_ = false;
    bool applying_theme_ = false;
    QAction* shelf_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* cut_action_ = nullptr;
    QAction* copy_action_ = nullptr;
    QAction* paste_action_ = nullptr;
    QAction* learn_action_ = nullptr;
    QAction* ignore_action_ = nullptr;
    QAction* name_action_ = nullptr;
    QAction* next_note_action_ = nullptr;
    QAction* prev_note_action_ = nullptr;
    QAction* apply_action_ = nullptr;
    QAction* details_action_ = nullptr;
    QAction* copyright_action_ = nullptr;
    QAction* contents_action_ = nullptr;
    QAction* cover_action_ = nullptr;
    QAction* export_action_ = nullptr;
    QPushButton* cover_button_ = nullptr;
    QAction* theme_actions_[4] = {};
    QAction* light_action_ = nullptr;
    QAction* dark_action_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QPushButton* light_switch_ = nullptr;
    QPushButton* dark_switch_ = nullptr;
};

}  // namespace inkwell
