#include "drawer.hpp"

#include <QAbstractButton>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace inkwell {

class EdgeTab : public QAbstractButton {
public:
    explicit EdgeTab(const QString& label, QWidget* parent = nullptr) : QAbstractButton(parent) {
        setText(label);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override {
        QFont font = this->font();
        font.setPointSize(9);
        font.setBold(true);
        const int along = QFontMetrics(font).horizontalAdvance(text()) + 48;
        return {34, qMax(84, along)};
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

    void setActive(bool active) {
        active_ = active;
        update();
    }

    void setColors(const QColor& active, const QColor& idle, const QColor& active_text, const QColor& idle_text) {
        active_color_ = active;
        idle_color_ = idle;
        active_text_ = active_text;
        idle_text_ = idle_text;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(active_ ? active_color_ : idle_color_);
        painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 8, 8);
        painter.setPen(active_ ? active_text_ : idle_text_);
        QFont font = this->font();
        font.setPointSize(9);
        font.setBold(true);
        painter.setFont(font);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.rotate(-90);
        painter.drawText(QRectF(-height() / 2.0, -width() / 2.0, height(), width()), Qt::AlignCenter, text());
    }

private:
    bool active_ = false;
    QColor active_color_{28, 25, 21};
    QColor idle_color_{154, 115, 68};
    QColor active_text_{255, 253, 248};
    QColor idle_text_{255, 248, 236};
};

SideDrawer::SideDrawer(DrawerSide side, int panel_width, QWidget* parent)
    : QWidget(parent), panel_width_(panel_width), side_(side) {
    tabs_ = new QWidget(this);
    tabs_->setFixedWidth(34);
    tab_layout_ = new QVBoxLayout(tabs_);
    tab_layout_->setContentsMargins(0, 16, 0, 16);
    tab_layout_->setSpacing(8);
    tab_layout_->addStretch();

    stack_ = new QStackedWidget(this);
    rule_ = new QWidget(this);
    rule_->setFixedWidth(1);
    rule_->setAutoFillBackground(true);
    panel_ = new QWidget(this);
    panel_->setAutoFillBackground(true);
    auto* panel_layout = new QHBoxLayout(panel_);
    panel_layout->setContentsMargins(0, 0, 0, 0);
    panel_layout->setSpacing(0);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    if (side_ == DrawerSide::Left) {
        panel_layout->addWidget(stack_, 1);
        panel_layout->addWidget(rule_);
        row->addWidget(panel_, 1);
        row->addWidget(tabs_);
    } else {
        panel_layout->addWidget(rule_);
        panel_layout->addWidget(stack_, 1);
        row->addWidget(tabs_);
        row->addWidget(panel_, 1);
    }
    applyWidth();
}

int SideDrawer::addPage(const QString& title, QWidget* page) {
    auto* tab = new EdgeTab(title, tabs_);
    const int index = tabs_widgets_.size();
    tab_layout_->insertWidget(index, tab);
    stack_->addWidget(page);
    tabs_widgets_.push_back(tab);
    connect(tab, &QAbstractButton::clicked, this, [this, index](bool) {
        if (open_ && current_ == index) {
            setOpen(false);
        } else {
            openPage(index);
        }
    });
    refreshTabs();
    applyWidth();
    return index;
}

void SideDrawer::openPage(int index) {
    if (index < 0 || index >= stack_->count()) {
        return;
    }
    current_ = index;
    stack_->setCurrentIndex(index);
    setOpen(true);
}

void SideDrawer::setOpen(bool open) {
    open_ = open;
    panel_->setVisible(open);
    refreshTabs();
    applyWidth();
}

void SideDrawer::polish(const DeskTheme& theme) {
    QPalette desk_palette = tabs_->palette();
    desk_palette.setColor(QPalette::Window, theme.desk);
    desk_palette.setColor(QPalette::Base, theme.desk);
    tabs_->setAutoFillBackground(true);
    tabs_->setPalette(desk_palette);
    tabs_->setStyleSheet(QStringLiteral("background: %1;").arg(theme.desk.name()));

    QPalette panel_palette = panel_->palette();
    panel_palette.setColor(QPalette::Window, theme.panel);
    panel_palette.setColor(QPalette::Base, theme.panel);
    panel_palette.setColor(QPalette::WindowText, theme.ink);
    panel_->setPalette(panel_palette);
    stack_->setAutoFillBackground(true);
    stack_->setPalette(panel_palette);
    for (int i = 0; i < stack_->count(); ++i) {
        QWidget* page = stack_->widget(i);
        page->setAutoFillBackground(true);
        page->setPalette(panel_palette);
    }

    for (EdgeTab* tab : tabs_widgets_) {
        tab->setColors(theme.accent, theme.tab_idle, theme.accent_ink, theme.tab_idle_text);
    }
    QPalette rule_palette = rule_->palette();
    rule_palette.setColor(QPalette::Window, theme.rule);
    rule_->setPalette(rule_palette);
}

void SideDrawer::applyWidth() {
    constexpr int kTabs = 34;
    panel_->setFixedWidth(open_ ? panel_width_ : 0);
    setFixedWidth(kTabs + (open_ ? panel_width_ : 0));
}

void SideDrawer::refreshTabs() {
    for (int i = 0; i < tabs_widgets_.size(); ++i) {
        tabs_widgets_[i]->setActive(open_ && i == current_);
    }
}

}  // namespace inkwell
