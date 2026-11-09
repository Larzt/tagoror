#include "ui/welcome.hpp"

#include "core/features.hpp"
#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/overlay.hpp"
#include "ui/switch.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

namespace {

/// Its size hint is its content's: the Overlay sizes the dialog from it, and
/// only when the panel is too short does the list scroll.
class FitScroll : public QScrollArea {
public:
    QSize sizeHint() const override {
        return widget() ? widget()->sizeHint() + QSize(2 * frameWidth(), 2 * frameWidth())
                        : QScrollArea::sizeHint();
    }
};

/// A row that flips its switch wherever it is clicked.
class FeatureRow : public QWidget {
public:
    Switch *toggle = nullptr;

    FeatureRow() {
        setObjectName("setRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && toggle &&
            !toggle->geometry().contains(e->position().toPoint()))
            toggle->flip();
    }
};

QLabel *sectionHead(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("meta");
    l->setContentsMargins(8, 6, 0, 2);
    return l;
}

}  // namespace

namespace Welcome {

void open(const Theme *theme, QWidget *host, const QStringList &off,
          std::function<void(const QStringList &)> done) {
    auto *ov = new Overlay(theme, host, L("Bienvenido a Tagoror"));
    ov->setMaxWidth(400);

    // Shared by the switches and the closing: whatever is marked when it closes.
    auto chosen = std::make_shared<QStringList>(off);

    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(8);

    auto *intro = new QLabel(L("Elige qué quieres usar. Las notas de texto siempre están, y "
                               "puedes cambiarlo cuando quieras en Ajustes → General."));
    intro->setObjectName("setRowText");
    intro->setWordWrap(true);
    intro->setMinimumWidth(24);   // see *Card widths*
    col->addWidget(intro);

    auto *list = new QWidget;
    list->setObjectName("welcomeList");
    auto *rows = new QVBoxLayout(list);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    bool pages = false, types = false;
    for (const Features::Info &f : Features::all()) {
        if (f.page && !pages) {
            rows->addWidget(sectionHead(L("PÁGINAS")));
            pages = true;
        }
        if (!f.page && !types) {
            rows->addWidget(sectionHead(L("TIPOS DE NOTA")));
            types = true;
        }
        auto *row = new FeatureRow;
        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(8, 6, 8, 6);
        l->setSpacing(10);
        auto *icon = new QLabel;
        icon->setFixedSize(16, 16);
        icon->setPixmap(paintIcon(f.icon, theme->accent, 16).pixmap(16, 16));
        l->addWidget(icon, 0, Qt::AlignVCenter);

        auto *texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(1);
        auto *name = new ElidedLabel(L(f.name), QColor(Theme::fg()));
        name->setObjectName("setRowText");
        texts->addWidget(name);
        auto *hint = new ElidedLabel(L(f.hint), QColor(Theme::muted()));
        hint->setObjectName("meta");
        hint->setToolTip(L(f.hint));
        texts->addWidget(hint);
        l->addLayout(texts, 1);

        const QString key = QString::fromLatin1(f.key);
        row->toggle = new Switch(!off.contains(key), theme, [chosen, key](bool on) {
            chosen->removeAll(key);
            if (!on) chosen->append(key);
        });
        l->addWidget(row->toggle, 0, Qt::AlignVCenter);
        rows->addWidget(row);
    }

    auto *scroll = new FitScroll;
    scroll->setObjectName("welcomeScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(list);
    scroll->viewport()->setObjectName("welcomeViewport");
    scroll->viewport()->setAutoFillBackground(false);
    list->setAutoFillBackground(false);
    col->addWidget(scroll, 1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 2, 0, 0);
    buttons->addStretch();
    auto *start = new QToolButton;
    start->setObjectName("pickSave");
    start->setText(L("Empezar"));
    start->setCursor(Qt::PointingHandCursor);
    start->setFocusPolicy(Qt::TabFocus);
    QObject::connect(start, &QToolButton::clicked, ov, [ov] { ov->close(); });
    buttons->addWidget(start);
    col->addLayout(buttons);

    ov->setContent(form);
    ov->closed = [chosen, done] {
        if (done) done(*chosen);
    };
    ov->open(start);
}

}  // namespace Welcome
