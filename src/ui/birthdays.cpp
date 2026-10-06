#include "ui/birthdays.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

#include "ui/keynav.hpp"
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

constexpr int kAvatarRow = 28;     // avatar of a row
constexpr int kAvatarCard = 38;    // avatar of the highlighted card
constexpr int kSoonDays = 7;       // up to here the countdown is in the accent

/// A stable tint per person, derived from the name (not the position) so it
/// does not change when others are added. Hand-rolled rather than qHash,
/// which Qt 6 seeds randomly per process.
QColor nameTint(const QString &name) {
    unsigned h = 2166136261u;
    for (const QChar &c : name) {
        h ^= unsigned(c.unicode());
        h *= 16777619u;
    }
    // Fixed saturation and value: only the hue varies, so nobody's colour reads
    // worse than the others' on the dark background.
    return QColor::fromHsv(int(h % 360u), 95, 205);
}

/// Month name in the interface language, upper case. Never
/// QLocale::system(): the language is the setting.
QString monthName(int month) {
    return Lang::locale().toString(QDate(2000, month, 1), "MMMM").toUpper();
}

/// Circle with the initials. Painted rather than a QLabel, which would demand
/// the width of its text (see *Card widths* in CLAUDE.md).
class Avatar : public QWidget {
public:
    Avatar(const QString &initials, const QColor &tint, int px, QWidget *parent = nullptr)
        : QWidget(parent), m_text(initials), m_tint(tint), m_px(px) {
        setFixedSize(px, px);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor fill = m_tint;
        fill.setAlpha(46);
        p.setPen(QPen(QColor(m_tint.red(), m_tint.green(), m_tint.blue(), 110), 1.0));
        p.setBrush(fill);
        p.drawEllipse(QRectF(0.5, 0.5, m_px - 1.0, m_px - 1.0));

        QFont f = font();
        f.setPixelSize(m_px <= 30 ? 11 : 14);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(m_tint);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    QColor m_tint;
    int m_px;
};

/// A list row: a plain QWidget with WA_StyledBackground so the stylesheet
/// paints its highlight.
class BirthdayRow : public QWidget {
public:
    explicit BirthdayRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("bdayRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { if (click) click(); });
    }

    /// Assigned after construction: the popup anchors to the row itself.
    std::function<void()> click;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && click)
            click();
    }
};

}  // namespace

BirthdayView::BirthdayView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("birthdays");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(3);
    m_layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(150);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    col->addWidget(scroll, 1);

    refresh();
}

void BirthdayView::setSource(const QList<Birthday *> *list) {
    m_list = list;
    refresh();
}

void BirthdayView::setTheme(const Theme &theme) {
    m_theme = theme;
    refresh();
}

void BirthdayView::setByMonth(bool on) {
    if (on == m_byMonth) return;
    m_byMonth = on;
    refresh();
}

/// Sorted by time left: in December the next one is in January. This is also
/// what decides who is highlighted. Ties are broken by name so two people on
/// the same day do not swap places between repaints.
QList<Birthday *> BirthdayView::sorted() const {
    QList<Birthday *> found;
    if (!m_list) return found;

    for (Birthday *b : *m_list)
        if (b->isValid()) found.append(b);

    std::sort(found.begin(), found.end(), [](Birthday *a, Birthday *b) {
        const int da = a->daysUntil(), db = b->daysUntil();
        return da != db ? da < db : a->name.localeAwareCompare(b->name) < 0;
    });
    return found;
}

/// The other order: January to December, the whole year's agenda.
QList<Birthday *> BirthdayView::byCalendar() const {
    QList<Birthday *> found;
    if (!m_list) return found;

    for (Birthday *b : *m_list)
        if (b->isValid()) found.append(b);

    std::sort(found.begin(), found.end(), [](Birthday *a, Birthday *b) {
        if (a->month != b->month) return a->month < b->month;
        if (a->day != b->day) return a->day < b->day;
        return a->name.localeAwareCompare(b->name) < 0;
    });
    return found;
}

void BirthdayView::refresh() {
    // Clear everything but the trailing stretch. Hidden as well as deleted: out
    // of the layout they stay painted and event-handling until deleteLater() runs.
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    // The highlighted one always comes from the closeness order: "who is next"
    // does not depend on how the list is viewed.
    const QList<Birthday *> all = sorted();
    int at = 0;

    if (all.isEmpty()) {
        auto *empty = new QLabel(L("Todavía no hay cumpleaños"));
        empty->setObjectName("meta");
        empty->setWordWrap(true);   // see *Card widths*: it must not demand its width
        empty->setContentsMargins(7, 8, 7, 4);
        m_layout->insertWidget(at++, empty);
        m_layout->insertWidget(at++, buildAddRow());
        return;
    }

    // Highlighted: those tied for soonest. Usually one; several when people share
    // the day, and picking one would be choosing for the user.
    const int soonest = all.first()->daysUntil();
    int highlighted = 0;
    while (highlighted < all.size() && all.at(highlighted)->daysUntil() == soonest) {
        m_layout->insertWidget(at++, buildHighlightCard(all.at(highlighted)));
        ++highlighted;
    }

    auto *line = new QFrame;
    line->setObjectName("calSeparator");
    line->setFixedHeight(1);
    m_layout->insertWidget(at++, line);

    // By month the list is the whole year, highlighted ones included: someone
    // missing from their month reads as a bug. By closeness it continues the card
    // above, so repeating them is redundant.
    const QList<Birthday *> list = m_byMonth ? byCalendar() : all.mid(highlighted);
    m_layout->insertWidget(at++, buildHeader(int(list.size())));

    int lastMonth = 0;
    for (Birthday *b : list) {
        // By closeness the month is that of the next turn (so next January follows
        // December); by calendar it is simply theirs.
        const int month = m_byMonth ? b->month : b->nextDate().month();
        if (month != lastMonth) {
            lastMonth = month;
            m_layout->insertWidget(at++, buildMonthHeader(month));
        }
        m_layout->insertWidget(at++, buildRow(b));
    }

    if (list.isEmpty()) {
        auto *empty = new QLabel(L("Ninguno más por ahora"));
        empty->setObjectName("meta");
        empty->setWordWrap(true);
        empty->setContentsMargins(7, 8, 7, 4);
        m_layout->insertWidget(at++, empty);
    }

    m_layout->insertWidget(at++, buildAddRow());
}

/// Month separator: the name and a rule to the edge.
QWidget *BirthdayView::buildMonthHeader(int month) {
    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(4, 9, 4, 3);
    l->setSpacing(7);

    auto *name = new QLabel(monthName(month));
    name->setObjectName("bdayMonth");
    l->addWidget(name);

    auto *rule = new QFrame;
    rule->setObjectName("bdayMonthRule");
    rule->setFixedHeight(1);
    l->addWidget(rule, 1);
    return row;
}

QWidget *BirthdayView::buildHeader(int listed) {
    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(4, 6, 4, 2);
    l->setSpacing(4);

    // The two orders as two small tabs; the lit one doubles as the section title.
    auto tab = [this, l](const QString &text, bool chosen, bool value) {
        auto *b = new QToolButton;
        b->setObjectName("bdayTab");
        b->setText(text);
        b->setProperty("chosen", chosen);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, value] {
            if (value == m_byMonth) return;
            m_byMonth = value;
            refresh();
            emit orderChanged(m_byMonth);
        });
        l->addWidget(b);
    };
    tab(L("PRÓXIMOS"), !m_byMonth, false);
    tab(L("MESES"), m_byMonth, true);
    l->addStretch();

    // The count differs between orders: by closeness, those after the highlighted
    // card; by month, all of them. Two different labels make that visible.
    auto *count = new QLabel(listed == 0 ? QString()
                                         : (m_byMonth ? L("%1 EN TOTAL").arg(listed)
                                                      : L("%1 MÁS").arg(listed)));
    count->setObjectName("meta");
    count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->addWidget(count);
    return row;
}

QWidget *BirthdayView::buildHighlightCard(Birthday *b) {
    const bool isToday = b->isToday();

    auto *card = new BirthdayRow;
    card->setObjectName("bdayToday");
    card->setCursor(Qt::PointingHandCursor);
    // Clicking the card opens the same editor as a row; the buttons inside keep
    // their own clicks.
    card->click = [this, b, card] { emit editRequested(b, card); };

    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(10, 9, 10, 9);
    col->setSpacing(8);

    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(9);
    head->addWidget(new Avatar(b->initials(), nameTint(b->name), kAvatarCard), 0,
                    Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(1);

    auto *name = new ElidedLabel(b->name.isEmpty() ? L("Sin nombre") : b->name,
                                 QColor(Theme::fg()));
    name->setObjectName("bdayName");
    name->setToolTip(b->name);
    texts->addWidget(name);

    // Today's needs no date; an upcoming one does, since the chip only says how
    // long is left and "in 3 weeks" is not a day of the month.
    QStringList meta;
    if (!isToday) meta << b->dateLabel();
    if (const QString sub = b->subtitle(); !sub.isEmpty()) meta << sub;
    auto *metaLabel = new ElidedLabel(meta.join(" · ").toUpper(), QColor(Theme::muted()));
    metaLabel->setObjectName("meta");
    texts->addWidget(metaLabel);
    head->addLayout(texts, 1);

    // Chip: already greeted, or how long is left.
    auto *chip = new QLabel(b->greeted() ? L("FELICITADO") : b->whenLabel().toUpper());
    chip->setObjectName(b->greeted() ? "bdayDoneChip" : "bdayTodayChip");
    head->addWidget(chip, 0, Qt::AlignTop);
    col->addLayout(head);

    // The buttons get a row of their own: they carry text, and a row of fixed
    // widths is the *Card widths* trap.
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);

    // While ringing, stopping it comes first.
    if (b->ringing) {
        auto *stop = new QPushButton(L("Detener aviso"));
        stop->setFocusPolicy(Qt::TabFocus);
        stop->setObjectName("bdayStop");   // the red lives in the stylesheet
        stop->setCursor(Qt::PointingHandCursor);
        connect(stop, &QPushButton::clicked, this, [this, b] { emit dismissRequested(b); });
        actions->addWidget(stop);
    } else if (isToday) {
        // Only on the day itself: greeting months early means nothing, and the mark
        // is stored per year.
        auto *greet = new QPushButton(b->greeted() ? L("Sin felicitar") : L("Felicitar"));
        greet->setCursor(Qt::PointingHandCursor);
        greet->setFocusPolicy(Qt::TabFocus);
        greet->setToolTip(L("Marca que ya le has felicitado este año"));
        connect(greet, &QPushButton::clicked, this, [this, b] { emit greetToggled(b); });
        actions->addWidget(greet);
    }

    auto *remind = new QToolButton;
    remind->setObjectName("bdayGhost");
    remind->setText(b->remindAt.isValid()
                        ? L("Aviso a las %1").arg(b->remindAt.toString("HH:mm"))
                        : L("Avisarme…"));
    remind->setCursor(Qt::PointingHandCursor);
    connect(remind, &QToolButton::clicked, this,
            [this, b, remind] { emit remindRequested(b, remind); });
    actions->addWidget(remind);
    actions->addStretch();
    col->addLayout(actions);

    return card;
}

QWidget *BirthdayView::buildRow(Birthday *b) {
    auto *row = new BirthdayRow;
    row->click = [this, b, row] { emit editRequested(b, row); };

    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(6, 5, 7, 5);
    l->setSpacing(9);
    l->addWidget(new Avatar(b->initials(), nameTint(b->name), kAvatarRow), 0,
                 Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);

    auto *name = new ElidedLabel(b->name.isEmpty() ? L("Sin nombre") : b->name,
                                 QColor(Theme::fg()));
    name->setObjectName("bdayRowName");
    name->setToolTip(b->name);
    texts->addWidget(name);

    // The second line may be empty (no year, no relation): then it is not built.
    if (const QString sub = b->subtitle(); !sub.isEmpty()) {
        auto *meta = new ElidedLabel(sub, QColor(Theme::muted()));
        meta->setObjectName("meta");
        texts->addWidget(meta);
    }
    l->addLayout(texts, 1);

    // A small bell when an alarm is set.
    if (b->remindAt.isValid()) {
        auto *bell = new QLabel;
        bell->setFixedSize(11, 11);
        bell->setPixmap(paintIcon("bell", QColor(Theme::muted()), 11).pixmap(11, 11));
        bell->setToolTip(L("Aviso a las %1").arg(b->remindAt.toString("HH:mm")));
        l->addWidget(bell, 0, Qt::AlignVCenter);
    }

    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);

    auto *date = new QLabel(b->dateLabel());
    date->setObjectName("bdayDate");
    date->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    right->addWidget(date);

    auto *when = new QLabel(b->whenLabel());
    when->setObjectName("bdayWhen");
    when->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // This week's in the accent, the rest dimmed: a page full of accent colour
    // highlights nothing.
    when->setProperty("soon", b->daysUntil() <= kSoonDays);
    right->addWidget(when);
    l->addLayout(right);

    return row;
}

/// The add row, styled like the others so it reads as "the next one".
QWidget *BirthdayView::buildAddRow() {
    auto *add = new BirthdayRow;
    auto *l = new QHBoxLayout(add);
    l->setContentsMargins(6, 6, 7, 6);
    l->setSpacing(9);

    auto *slot = new QLabel;
    slot->setFixedSize(kAvatarRow, kAvatarRow);
    slot->setPixmap(paintIcon("checkdots", QColor(Theme::muted()), kAvatarRow)
                        .pixmap(kAvatarRow, kAvatarRow));
    l->addWidget(slot);

    auto *text = new QLabel(L("Añadir un cumpleaños"));
    text->setObjectName("bdayRowName");
    l->addWidget(text, 1);

    add->click = [this, add] { emit addRequested(add); };
    return add;
}
