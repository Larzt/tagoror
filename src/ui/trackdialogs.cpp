#include "ui/trackdialogs.hpp"

#include <memory>

#include <QAbstractButton>
#include <QCheckBox>
#include <QCompleter>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/lang.hpp"
#include "core/platform.hpp"
#include "ui/keynav.hpp"
#include "ui/overlay.hpp"
#include "ui/popup.hpp"

namespace trackdlg {

namespace {

const QColor kRed("#ff7a6b");

/// "30/10/2026", "30/10", "30-10-26", "hoy", "ayer". Invalid if none of those.
QDate parseDate(const QString &text) {
    const QString t = text.trimmed().toLower();
    const QDate today = QDate::currentDate();
    if (t == "hoy" || t == "today") return today;
    if (t == "ayer" || t == "yesterday") return today.addDays(-1);
    QString norm = t;
    norm.replace('-', '/').replace('.', '/');
    const QStringList parts = norm.split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.size() > 3) return {};
    bool okD = false, okM = false, okY = true;
    const int d = parts.at(0).toInt(&okD);
    const int m = parts.at(1).toInt(&okM);
    int y = today.year();
    if (parts.size() == 3) {
        y = parts.at(2).toInt(&okY);
        if (parts.at(2).size() <= 2) y += 2000;
    }
    if (!okD || !okM || !okY) return {};
    return QDate(y, m, d);
}

/// "11:00", "11.30", "11h30", "1130" or "11".
QTime parseTime(const QString &text) {
    static const QRegularExpression re(R"(^(\d{1,2})(?:\s*[:.h]\s*(\d{2})|(\d{2}))?$)");
    const auto m = re.match(text.trimmed().toLower());
    if (!m.hasMatch()) return {};
    const int h = m.captured(1).toInt();
    const QString mm = m.captured(2).isEmpty() ? m.captured(3) : m.captured(2);
    return QTime(h, mm.isEmpty() ? 0 : mm.toInt());
}

QString timeText(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm"); }

QLabel *fieldLabel(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("setFieldLabel");
    return l;
}

/// A label over its field, as in the design's forms.
QWidget *labelled(const QString &label, QWidget *field) {
    auto *box = new QWidget;
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    col->addWidget(fieldLabel(label));
    col->addWidget(field);
    return box;
}

QLineEdit *monoField(const QString &text) {
    auto *e = new QLineEdit(text);
    e->setObjectName("setField");
    e->setMinimumWidth(40);
    return e;
}

QToolButton *button(const char *name, const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName(name);
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

/// The project picker's face: its dot and name, or a dash for none.
void showProject(QToolButton *b, const Project *p, bool withRate) {
    if (!p) {
        b->setIcon(paintIcon("minus", QColor(Theme::muted()), 14));
        b->setText(L("Sin proyecto"));
        return;
    }
    b->setIcon(paintIcon("glyph-dot", p->tint(), 14));
    b->setText(withRate && p->hasRate() ? p->name + "   " + Track::rateLabel(p->rate) : p->name);
}

/// One of the eight project colours, chosen or not.
class Swatch : public QAbstractButton {
public:
    Swatch(const QColor &color, QWidget *parent = nullptr) : QAbstractButton(parent), m_color(color) {
        setCheckable(true);
        setAutoExclusive(true);
        setFixedSize(26, 26);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c = QRectF(rect()).center();
        if (isChecked() || keynav::showsFocus(this)) {
            p.setPen(QPen(QColor(Theme::fg()), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 11.5, 11.5);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawEllipse(c, 8, 8);
    }

private:
    QColor m_color;
};

}  // namespace

void editSession(const Theme *theme, QWidget *host, const Session *session,
                 const QList<Session *> &sessions, const QList<Project *> &projects,
                 std::function<void(const SessionDraft &)> commit, std::function<void()> remove) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Copies, not pointers: a sync may replace the lists while the form is open.
    struct Other {
        QString task;
        qint64 start, end;
    };
    QList<Other> others;
    QStringList tasks;
    qint64 lastEnd = 0;
    for (const Session *s : sessions) {
        if (!tasks.contains(s->task, Qt::CaseInsensitive) && !s->task.isEmpty()) tasks << s->task;
        if (s == session) continue;
        others << Other{s->task, s->startMs, s->endOr(now)};
        if (!s->running()) lastEnd = qMax(lastEnd, s->endMs);
    }
    auto projs = std::make_shared<QList<Project>>();
    for (const Project *p : projects)
        if (!p->archived || (session && session->project == p->id)) projs->append(*p);

    struct State {
        QString project;
        double rate = -1;
        qint64 pausedMs = 0;
        bool running = false;
        qint64 startMs = 0, endMs = 0;
        bool valid = false;
    };
    auto st = std::make_shared<State>();
    qint64 start = 0, end = 0;
    if (session) {
        st->project = session->project;
        st->rate = session->rate;
        st->pausedMs = session->pausedMs;
        st->running = session->running();
        start = session->startMs;
        end = session->endOr(now);
    } else {
        // The gap that was forgotten: from the end of the last session to now. If
        // that was another day, half an hour back is a better guess.
        end = now - now % 60000;
        start = (lastEnd > 0 && lastEnd < end &&
                 QDateTime::fromMSecsSinceEpoch(lastEnd).date() == QDate::currentDate())
                    ? lastEnd - lastEnd % 60000
                    : end - 30 * 60000;
    }
    const auto findProject = [projs](const QString &id) -> const Project * {
        for (const Project &p : *projs)
            if (p.id == id) return &p;
        return nullptr;
    };
    if (!session)
        if (const Project *p = findProject(st->project)) st->rate = p->rate;

    auto *ov = new Overlay(theme, host, session ? L("Editar sesión") : L("Añadir sesión"));
    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);

    auto *task = new QLineEdit(session ? session->task : QString());
    task->setPlaceholderText(L("¿En qué trabajaste?"));
    auto *completer = new QCompleter(tasks, task);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    task->setCompleter(completer);
    col->addWidget(labelled(L("Tarea"), task));

    auto *pick = new QToolButton;
    pick->setObjectName("trackPicker");
    pick->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pick->setCursor(Qt::PointingHandCursor);
    pick->setFocusPolicy(Qt::TabFocus);
    pick->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    showProject(pick, findProject(st->project), true);
    col->addWidget(labelled(L("Proyecto"), pick));

    const QDateTime startAt = QDateTime::fromMSecsSinceEpoch(start);
    auto *date = monoField(startAt.date().toString("dd/MM/yyyy"));
    auto *from = monoField(timeText(start));
    auto *to = monoField(st->running ? L("en marcha") : timeText(end));
    auto *dur = monoField(Track::hm(end - start - st->pausedMs));
    to->setEnabled(!st->running);
    dur->setEnabled(!st->running);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->addWidget(labelled(L("Fecha"), date), 0, 0, 1, 3);
    grid->addWidget(labelled(L("Inicio"), from), 1, 0);
    grid->addWidget(labelled(L("Fin"), to), 1, 1);
    grid->addWidget(labelled(L("Duración"), dur), 1, 2);
    col->addLayout(grid);

    auto *warn = new QLabel;
    warn->setObjectName("trackWarn");
    warn->setWordWrap(true);
    warn->hide();
    col->addWidget(warn);
    auto *amount = new QLabel;
    amount->setObjectName("trackAmount");
    amount->hide();
    col->addWidget(amount);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    if (session && remove) {
        auto *del = button("quitBtn", L("Borrar"));
        QObject::connect(del, &QToolButton::clicked, ov, [ov, remove] {
            ov->close();
            remove();
        });
        buttons->addWidget(del);
    }
    buttons->addStretch();
    auto *cancel = button("pickCancel", L("Cancelar"));
    QObject::connect(cancel, &QToolButton::clicked, ov, [ov] { ov->close(); });
    auto *save = button("pickSave", L("Guardar"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    col->addLayout(buttons);

    enum Field { Any, Start, End, Dur };
    // Recomputes whichever of end and duration was not typed, and everything
    // that depends on the times: the overlap warning, the amount and Guardar.
    auto update = [=](Field source) {
        const QDate d = parseDate(date->text());
        const QTime t0 = parseTime(from->text());
        QTime t1 = parseTime(to->text());
        qint64 len = Track::parseDuration(dur->text());
        st->valid = false;
        if (d.isValid() && t0.isValid()) {
            const qint64 s0 = QDateTime(d, t0).toMSecsSinceEpoch();
            qint64 s1 = 0;
            if (st->running) {
                len = qMax<qint64>(0, now - s0 - st->pausedMs);
                dur->setText(Track::hm(len));
                st->valid = s0 < now;
            } else if (source == Dur && len > 0) {
                s1 = s0 + len + st->pausedMs;
                to->setText(timeText(s1));
                st->valid = true;
            } else if (t1.isValid()) {
                s1 = QDateTime(d, t1).toMSecsSinceEpoch();
                if (s1 <= s0) s1 = QDateTime(d.addDays(1), t1).toMSecsSinceEpoch();   // past midnight
                len = s1 - s0 - st->pausedMs;
                if (source != Dur) dur->setText(Track::hm(len));
                st->valid = len > 0;
            }
            st->startMs = s0;
            st->endMs = s1;
        }
        save->setEnabled(st->valid && !task->text().trimmed().isEmpty());

        // The biggest overlap, if any: worth knowing, never a reason to refuse.
        warn->hide();
        if (st->valid) {
            const qint64 e = st->running ? now : st->endMs;
            qint64 worst = 0;
            const Other *with = nullptr;
            for (const Other &o : others) {
                const qint64 overlap = qMin(e, o.end) - qMax(st->startMs, o.start);
                if (overlap > worst) {
                    worst = overlap;
                    with = &o;
                }
            }
            if (with && worst >= 60000) {
                warn->setText(L("Se solapa %1 con «%2» (%3–%4). Se guardará igualmente.")
                                  .arg(Track::words(worst), with->task, timeText(with->start),
                                       timeText(with->end)));
                warn->show();
            }
        }
        amount->setVisible(st->valid && st->rate >= 0);
        if (st->valid && st->rate >= 0) {
            const qint64 ms = (st->running ? now : st->endMs) - st->startMs - st->pausedMs;
            amount->setText(QString("%1 h × %2 = %3")
                                .arg(Track::hm(ms), Track::rateLabel(st->rate),
                                     Track::money(ms / 3600000.0 * st->rate)));
        }
        ov->relayout();
    };
    QObject::connect(date, &QLineEdit::textEdited, ov, [update] { update(Any); });
    QObject::connect(from, &QLineEdit::textEdited, ov, [update] { update(Start); });
    QObject::connect(to, &QLineEdit::textEdited, ov, [update] { update(End); });
    QObject::connect(dur, &QLineEdit::textEdited, ov, [update] { update(Dur); });
    QObject::connect(task, &QLineEdit::textChanged, ov, [update] { update(Any); });

    QObject::connect(pick, &QToolButton::clicked, ov, [=] {
        auto *menu = new Popup(*theme, pick);
        menu->addItem("minus", L("Sin proyecto"), QString(), [=] {
            st->project.clear();
            st->rate = -1;
            showProject(pick, nullptr, true);
            update(Any);
        });
        for (const Project &p : *projs) {
            const QString id = p.id;
            menu->addColorItem(p.tint(), p.name, p.hasRate() ? Track::rateLabel(p.rate) : QString(), [=] {
                const Project *chosen = findProject(id);
                st->project = id;
                // The same project keeps the session's own rate; another brings its own.
                st->rate = session && session->project == id ? session->rate
                                                              : (chosen ? chosen->rate : -1);
                showProject(pick, chosen, true);
                update(Any);
            });
        }
        menu->showBelow(pick);
    });

    auto submit = [=] {
        if (!save->isEnabled()) return;
        SessionDraft d;
        d.task = task->text().trimmed();
        d.project = st->project;
        d.startMs = st->startMs;
        d.endMs = st->running ? 0 : st->endMs;
        d.pausedMs = st->pausedMs;
        d.rate = st->rate;
        ov->close();
        commit(d);
    };
    QObject::connect(save, &QToolButton::clicked, ov, submit);
    for (QLineEdit *e : {task, date, from, to, dur})
        QObject::connect(e, &QLineEdit::returnPressed, ov, submit);

    update(Any);
    ov->setContent(form);
    ov->open(task);
}

void editProject(const Theme *theme, QWidget *host, const Project *project,
                 const QList<Project *> &projects, std::function<void(const ProjectDraft &)> commit) {
    auto *ov = new Overlay(theme, host, project ? L("Editar proyecto") : L("Nuevo proyecto"));
    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);

    auto *name = new QLineEdit(project ? project->name : QString());
    name->setPlaceholderText(L("Cliente, encargo o tema"));
    col->addWidget(labelled(L("Nombre"), name));

    // A new project takes the first colour nobody uses yet.
    int color = 0;
    if (project) {
        color = project->color;
    } else {
        QList<int> used;
        for (const Project *p : projects)
            if (!p->archived) used << p->color;
        for (int i = 0; i < Project::kColors; ++i)
            if (!used.contains(i)) {
                color = i;
                break;
            }
    }
    auto chosen = std::make_shared<int>(color);
    auto *swatches = new QWidget;
    // On a phone each swatch is a 44px target, and eight in a row do not fit:
    // two rows of four.
    QGridLayout *grid = mobileUi() ? new QGridLayout(swatches) : nullptr;
    QHBoxLayout *row = grid ? nullptr : new QHBoxLayout(swatches);
    QLayout *swatchLayout = grid ? static_cast<QLayout *>(grid) : row;
    swatchLayout->setContentsMargins(0, 0, 0, 0);
    swatchLayout->setSpacing(4);
    for (int i = 0; i < Project::kColors; ++i) {
        auto *s = new Swatch(Project::paletteColor(i));
        s->setChecked(i == color);
        QObject::connect(s, &QAbstractButton::clicked, ov, [chosen, i] { *chosen = i; });
        if (grid) grid->addWidget(s, i / 4, i % 4);
        else row->addWidget(s);
    }
    if (grid) grid->setColumnStretch(4, 1);
    else row->addStretch();
    col->addWidget(labelled(L("Color"), swatches));

    auto *client = new QLineEdit(project ? project->client : QString());
    client->setPlaceholderText(L("Para quién es"));
    col->addWidget(labelled(L("Cliente · opcional"), client));

    auto *rate = monoField(project && project->hasRate()
                               ? Lang::locale().toString(project->rate, 'f', 2)
                               : QString());
    rate->setPlaceholderText("0,00");
    auto *rateRow = new QWidget;
    auto *rl = new QHBoxLayout(rateRow);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);
    rl->addWidget(rate, 1);
    auto *unit = new QLabel(QStringLiteral("€/h"));
    unit->setObjectName("setFieldLabel");
    rl->addWidget(unit);
    col->addWidget(labelled(L("Tarifa por hora · opcional"), rateRow));
    auto *hint = new QLabel(L("Vacía: cuenta horas, sin importe."));
    hint->setObjectName("setFieldLabel");
    hint->setWordWrap(true);
    col->addWidget(hint);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    auto archived = std::make_shared<bool>(project && project->archived);
    QToolButton *archive = nullptr;
    if (project) {
        archive = button("pickCancel", project->archived ? L("Recuperar") : L("Archivar"));
        archive->setToolTip(project->archived ? L("Vuelve a las listas")
                                              : L("Lo oculta sin perder sus horas"));
        buttons->addWidget(archive);
    }
    buttons->addStretch();
    auto *cancel = button("pickCancel", L("Cancelar"));
    QObject::connect(cancel, &QToolButton::clicked, ov, [ov] { ov->close(); });
    auto *save = button("pickSave", L("Guardar"));
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    col->addLayout(buttons);

    auto validate = [=] {
        bool ok = false;
        Track::parseRate(rate->text(), &ok);
        hint->setText(ok ? L("Vacía: cuenta horas, sin importe.")
                         : L("Escribe un número, como 35 o 35,50."));
        hint->setStyleSheet(ok ? QString() : QString("color: %1;").arg(kRed.name()));
        save->setEnabled(ok && !name->text().trimmed().isEmpty());
    };
    QObject::connect(name, &QLineEdit::textChanged, ov, validate);
    QObject::connect(rate, &QLineEdit::textChanged, ov, validate);

    auto submit = [=] {
        if (!save->isEnabled()) return;
        bool ok = false;
        ProjectDraft d;
        d.name = name->text().trimmed();
        d.color = *chosen;
        d.client = client->text().trimmed();
        d.rate = Track::parseRate(rate->text(), &ok);
        d.archived = *archived;
        ov->close();
        commit(d);
    };
    QObject::connect(save, &QToolButton::clicked, ov, submit);
    for (QLineEdit *e : {name, client, rate}) QObject::connect(e, &QLineEdit::returnPressed, ov, submit);
    if (archive)
        QObject::connect(archive, &QToolButton::clicked, ov, [=] {
            // Archiving is saving with the flag flipped: whatever else was typed counts.
            *archived = !*archived;
            if (save->isEnabled()) submit();
            else *archived = !*archived;
        });

    validate();
    ov->setContent(form);
    ov->open(name);
}

void askRateScope(const Theme *theme, QWidget *host, const QString &project, double rate,
                  int pastCount, std::function<void(bool)> answer) {
    auto *ov = new Overlay(theme, host, L("Nueva tarifa"));
    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(12);
    const QString what = rate >= 0 ? Track::rateLabel(rate) : L("sin tarifa");
    auto *text = new QLabel(
        (pastCount == 1 ? L("«%1» ya tiene 1 sesión registrada. ¿Pasa también a %2?")
                        : L("«%1» ya tiene %3 sesiones registradas. ¿Pasan también a %2?"))
            .arg(project, what)
            .arg(pastCount));
    text->setObjectName("setRowText");
    text->setWordWrap(true);
    col->addWidget(text);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch();
    auto *only = button("pickCancel", L("Solo las nuevas"));
    auto *all = button("pickSave", L("También las pasadas"));
    buttons->addWidget(only);
    buttons->addWidget(all);
    col->addLayout(buttons);
    QObject::connect(only, &QToolButton::clicked, ov, [ov] { ov->close(); });
    QObject::connect(all, &QToolButton::clicked, ov, [ov, answer] {
        ov->close();
        answer(true);
    });
    ov->setContent(form);
    ov->open(all);
}

void exportCsv(const Theme *theme, QWidget *host, const QList<Session *> &sessions,
               const QList<Project *> &projects, const QDate &from, const QDate &to,
               const QString &project) {
    // Copies, as in the session form: the lists may change while it is open.
    auto own = std::make_shared<QList<Session>>();
    for (const Session *s : sessions) own->append(*s);
    auto projs = std::make_shared<QList<Project>>();
    for (const Project *p : projects) projs->append(*p);

    struct State {
        Track::CsvOptions opt;
        int period = 3;   ///< 0 this week, 1 this month, 2 last month, 3 custom.
    };
    auto st = std::make_shared<State>();
    st->opt.from = from;
    st->opt.to = to;
    st->opt.project = project;
    const QDate today = QDate::currentDate();
    const QDate monday = Track::weekStart(today);
    const QDate month(today.year(), today.month(), 1);
    const QList<QPair<QDate, QDate>> periods{{monday, monday.addDays(6)},
                                            {month, month.addMonths(1).addDays(-1)},
                                            {month.addMonths(-1), month.addDays(-1)}};
    for (int i = 0; i < periods.size(); ++i)
        if (periods.at(i).first == from && periods.at(i).second == to) st->period = i;

    auto *ov = new Overlay(theme, host, L("Exportar sesiones a CSV"));
    ov->setMaxWidth(420);
    auto *form = new QWidget;
    auto *col = new QVBoxLayout(form);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);

    auto *chips = new QWidget;
    auto *grid = new QGridLayout(chips);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);
    QList<QToolButton *> periodButtons;
    const QStringList names{L("Esta semana"), L("Este mes"), L("Mes pasado"), L("Personalizado")};
    for (int i = 0; i < names.size(); ++i) {
        auto *b = button("segButton", names.at(i));
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        b->setMinimumWidth(40);
        grid->addWidget(b, i / 3, i % 3);
        periodButtons << b;
    }
    auto *range = new QWidget;
    auto *rl = new QHBoxLayout(range);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);
    auto *fromEdit = monoField(from.toString("dd/MM/yyyy"));
    auto *toEdit = monoField(to.toString("dd/MM/yyyy"));
    rl->addWidget(fromEdit, 1);
    auto *dash = new QLabel(QStringLiteral("–"));
    dash->setObjectName("setFieldLabel");
    rl->addWidget(dash);
    rl->addWidget(toEdit, 1);
    grid->addWidget(range, 1, 1, 1, 2);
    col->addWidget(labelled(L("Periodo"), chips));

    auto *pick = new QToolButton;
    pick->setObjectName("trackPicker");
    pick->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pick->setCursor(Qt::PointingHandCursor);
    pick->setFocusPolicy(Qt::TabFocus);
    pick->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    col->addWidget(labelled(L("Proyecto"), pick));

    auto *summary = new QLabel;
    summary->setObjectName("trackAmount");
    col->addWidget(summary);
    auto *preview = new QPlainTextEdit;
    preview->setObjectName("trackPreview");
    preview->setReadOnly(true);
    preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    preview->setFocusPolicy(Qt::NoFocus);
    preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview->setFixedHeight(74);
    col->addWidget(preview);

    auto *perSession = new QCheckBox(L("Una fila por sesión"));
    perSession->setChecked(true);
    perSession->setToolTip(L("Sin marcar: una fila por día, proyecto y tarea"));
    auto *totals = new QCheckBox(L("Añadir fila de totales por proyecto"));
    col->addWidget(perSession);
    col->addWidget(totals);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch();
    auto *cancel = button("pickCancel", L("Cancelar"));
    auto *go = button("pickSave", L("Exportar…"));
    buttons->addWidget(cancel);
    buttons->addWidget(go);
    col->addLayout(buttons);
    QObject::connect(cancel, &QToolButton::clicked, ov, [ov] { ov->close(); });

    // The lists the core functions take, as pointers into the copies.
    auto sessionPtrs = [own] {
        QList<Session *> out;
        for (Session &s : *own) out << &s;
        return out;
    };
    auto projectPtrs = [projs] {
        QList<Project *> out;
        for (Project &p : *projs) out << &p;
        return out;
    };

    auto refresh = [=] {
        for (int i = 0; i < periodButtons.size(); ++i) {
            QToolButton *b = periodButtons.at(i);
            b->setProperty("chosen", i == st->period);
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
        range->setVisible(st->period == 3);
        if (st->period < 3) {
            st->opt.from = periods.at(st->period).first;
            st->opt.to = periods.at(st->period).second;
        } else {
            const QDate a = parseDate(fromEdit->text()), b = parseDate(toEdit->text());
            if (a.isValid()) st->opt.from = a;
            if (b.isValid()) st->opt.to = b;
        }
        const Project *chosen = nullptr;
        for (const Project &p : *projs)
            if (p.id == st->opt.project) chosen = &p;
        if (chosen) showProject(pick, chosen, false);
        else {
            pick->setIcon(paintIcon("minus", QColor(Theme::muted()), 14));
            pick->setText(st->opt.project == "-" ? L("Sin proyecto") : L("Todos los proyectos"));
        }
        st->opt.perSession = perSession->isChecked();
        st->opt.totals = totals->isChecked();

        int count = 0;
        qint64 ms = 0;
        double money = 0;
        bool rated = false;
        const QList<Session *> list = sessionPtrs();
        for (const Session *s : Track::inRange(list, st->opt.from, st->opt.to, st->opt.project)) {
            if (s->running()) continue;
            ++count;
            ms += s->durationMs();
            money += s->amount();
            rated |= s->hasRate();
        }
        QString text = (count == 1 ? L("1 sesión") : L("%1 sesiones").arg(count)) + " · " +
                       Track::words(ms);
        if (rated) text += " · " + Track::money(money);
        summary->setText(text);
        const QStringList lines = Track::csvLines(list, projectPtrs(), st->opt);
        preview->setPlainText(lines.mid(0, 4).join('\n'));
        go->setEnabled(count > 0 && st->opt.from <= st->opt.to);
    };
    for (int i = 0; i < periodButtons.size(); ++i)
        QObject::connect(periodButtons.at(i), &QToolButton::clicked, ov, [=] {
            st->period = i;
            refresh();
            if (i == 3) fromEdit->setFocus(Qt::OtherFocusReason);
        });
    QObject::connect(fromEdit, &QLineEdit::textEdited, ov, refresh);
    QObject::connect(toEdit, &QLineEdit::textEdited, ov, refresh);
    QObject::connect(perSession, &QCheckBox::toggled, ov, refresh);
    QObject::connect(totals, &QCheckBox::toggled, ov, refresh);
    QObject::connect(pick, &QToolButton::clicked, ov, [=] {
        auto *menu = new Popup(*theme, pick);
        menu->addItem("minus", L("Todos los proyectos"), QString(), [=] {
            st->opt.project.clear();
            refresh();
        });
        menu->addItem("minus", L("Sin proyecto"), QString(), [=] {
            st->opt.project = "-";
            refresh();
        });
        for (const Project &p : *projs) {
            const QString id = p.id;
            menu->addColorItem(p.tint(), p.name, p.client, [=] {
                st->opt.project = id;
                refresh();
            });
        }
        menu->showBelow(pick);
    });

    QObject::connect(go, &QToolButton::clicked, ov, [=] {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const bool wholeMonth = st->opt.from.day() == 1 &&
                                st->opt.to == st->opt.from.addMonths(1).addDays(-1);
        const QString name = wholeMonth ? st->opt.from.toString("'tagoror-'yyyy-MM'.csv'")
                                        : QString("tagoror-%1-%2.csv")
                                              .arg(st->opt.from.toString(Qt::ISODate),
                                                   st->opt.to.toString(Qt::ISODate));
        const QString file = QFileDialog::getSaveFileName(host, L("Exportar sesiones a CSV"),
                                                          dir + "/" + name, L("CSV (*.csv)"));
        if (file.isEmpty()) return;
        if (Track::writeCsv(file, Track::csvLines(sessionPtrs(), projectPtrs(), st->opt))) ov->close();
    });

    refresh();
    ov->setContent(form);
    ov->open(go);
}

}  // namespace trackdlg
