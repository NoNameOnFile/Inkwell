#pragma once

#include "theme.hpp"

#include <QVector>
#include <QWidget>

class QStackedWidget;
class QVBoxLayout;

namespace inkwell {

class EdgeTab;

enum class DrawerSide { Left, Right };

class SideDrawer : public QWidget {
    Q_OBJECT

public:
    explicit SideDrawer(DrawerSide side, int panel_width, QWidget* parent = nullptr);

    int addPage(const QString& title, QWidget* page);
    void openPage(int index);
    void setOpen(bool open);
    bool isOpen() const { return open_; }
    int currentIndex() const { return current_; }
    void polish(const DeskTheme& theme);

private:
    void applyWidth();
    void refreshTabs();

    QWidget* tabs_ = nullptr;
    QVBoxLayout* tab_layout_ = nullptr;
    QWidget* panel_ = nullptr;
    QWidget* rule_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QVector<EdgeTab*> tabs_widgets_;
    int panel_width_ = 320;
    DrawerSide side_ = DrawerSide::Right;
    int current_ = 0;
    bool open_ = true;
};

}  // namespace inkwell
