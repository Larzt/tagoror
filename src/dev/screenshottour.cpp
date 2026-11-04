#include "dev/screenshottour.hpp"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <cxxabi.h>
#include <memory>
#include <typeinfo>

#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/birthdays.hpp"
#include "ui/navbar.hpp"
#include "ui/palette.hpp"
#include "ui/panel.hpp"
#include "ui/planner.hpp"
#include "ui/settings.hpp"
#include "ui/timers.hpp"
#include "ui/tracking.hpp"

namespace {

constexpr int kSettleMs = 450;   ///< After opening something: layouts and timers settle.

/// The real class, also for those without Q_OBJECT (NavBar's items, Overlay…).
QString typeName(const QWidget *w) {
    int status = 0;
    std::unique_ptr<char, void (*)(void *)> d(
        abi::__cxa_demangle(typeid(*w).name(), nullptr, nullptr, &status), std::free);
    return status == 0 && d ? QString::fromLatin1(d.get()) : QString::fromLatin1(typeid(*w).name());
}

QString describe(const QWidget *w, const QRect &r) {
    QString s = typeName(w);
    if (!w->objectName().isEmpty()) s += "#" + w->objectName();
    s += QString(" x=%1 w=%2 minHint=%3").arg(r.x()).arg(r.width()).arg(w->minimumSizeHint().width());
    if (w->minimumWidth() > 0) s += QString(" minW=%1").arg(w->minimumWidth());
    if (w->maximumWidth() == w->minimumWidth() && w->minimumWidth() > 0) s += " (fixed)";
    QString text;
    if (auto *l = qobject_cast<const QLabel *>(w)) text = l->text();
    else if (auto *b = qobject_cast<const QAbstractButton *>(w)) text = b->text().isEmpty() ? b->toolTip() : b->text();
    if (!text.isEmpty()) s += " «" + text.left(40).replace('\n', ' ') + "»";
    return s;
}

QAbstractButton *buttonWithTip(QWidget *root, const QString &tip) {
    for (QAbstractButton *b : root->findChildren<QAbstractButton *>())
        if (b->toolTip() == tip) return b;
    return nullptr;
}

}  // namespace

ScreenshotTour::ScreenshotTour(Panel *p, const QString &dir) : QObject(p), m_panel(p), m_dir(dir) {
    m_dir.mkpath(".");
    seed();

    const auto page = [p](QWidget *w) { return [p, w] { p->openPage(w); }; };
    const auto settingsTab = [p](SettingsView::Tab t) {
        return [p, t] {
            p->openPage(p->m_settings);
            p->m_settings->setTab(t);
        };
    };
    const auto trackingTab = [p](TrackingView::Tab t) {
        return [p, t] {
            p->openPage(p->m_tracking);
            p->m_tracking->setTab(t);
        };
    };
    const auto plannerView = [p](PlannerView::View v) {
        return [p, v] {
            p->openPage(p->m_planner);
            p->m_planner->setView(v);
        };
    };
    const auto none = [] {};

    m_steps = {
        {"01-notas", [p] { p->showNotes(); }, none},
        {"02-planner", page(p->m_planner), none},
        {"02b-planner-dia", plannerView(PlannerView::Day), none},
        {"02c-planner-semana", plannerView(PlannerView::Week), none},
        {"02d-planner-mes", plannerView(PlannerView::Month), none},
        {"03-timers", page(p->m_timerView), none},
        {"04-cumpleanos", page(p->m_birthdays), none},
        {"05-seguimiento", trackingTab(TrackingView::Log), none},
        {"05b-seguimiento-resumen", trackingTab(TrackingView::Summary), none},
        {"05c-seguimiento-proyectos", trackingTab(TrackingView::Projects),
         [p] { p->m_tracking->setTab(TrackingView::Log); }},
        {"06-ajustes-general", settingsTab(SettingsView::General), none},
        {"06b-ajustes-apariencia", settingsTab(SettingsView::Appearance), none},
        {"06c-ajustes-sincronizar", settingsTab(SettingsView::Sync), none},
        {"06d-ajustes-atajos", settingsTab(SettingsView::Shortcuts), none},
        {"06e-ajustes-acerca", settingsTab(SettingsView::About),
         [p] { p->m_settings->setTab(SettingsView::General); }},
        {"10-menu-mas", [p] { p->showNotes(); p->openMoreMenu(p->m_moreBtn); }, none},
        {"11-nueva-nota", [p] { p->showNotes(); p->openNewNoteMenu(p->m_addBtn); }, none},
        {"12-paleta", [p] { p->showNotes(); p->openPalette(); }, none},
        {"13-busqueda", [p] { p->showNotes(); p->focusSearch(); }, none},
        {"14-planner-nuevo",
         [p] {
             p->openPage(p->m_planner);
             p->m_planner->setView(PlannerView::Week);
             if (QAbstractButton *b = buttonWithTip(p->m_planner, L("Nuevo evento o tarea"))) b->click();
         },
         none},
        {"15-timer-nuevo",
         [p] {
             p->openPage(p->m_timerView);
             p->m_timerView->toggleCreator();
         },
         [p] { p->m_timerView->toggleCreator(); }},
        {"16-cumple-nuevo",
         [p] {
             p->openPage(p->m_birthdays);
             p->openBirthdayEditor(nullptr, p->m_bdayAddBtn);
         },
         none},
        {"17-sesion-nueva",
         [p] {
             p->openPage(p->m_tracking);
             p->openSessionEditor(nullptr);
         },
         none},
        {"18-proyecto-nuevo",
         [p] {
             p->openPage(p->m_tracking);
             p->openProjectEditor(nullptr);
         },
         none},
        {"19-markdown",
         [p] {
             for (Note *n : p->m_store.notes())
                 if (n->type == Note::Markdown) {
                     p->openMarkdown(n);
                     break;
                 }
         },
         none},
    };

    // Once the window has been shown and the startup splash is gone.
    QTimer::singleShot(1500, this, &ScreenshotTour::next);
}

/// Something in every page, with long texts where they would overflow.
void ScreenshotTour::seed() {
    Panel *p = m_panel;
    Store &st = p->m_store;
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();

    auto *text = new Note;
    text->title = "Ideas para el viaje a Lanzarote con un título bastante largo";
    text->body = "Alquilar coche en el aeropuerto, visitar los Jameos del Agua, el "
                 "Mirador del Río y la Fundación César Manrique. Reservar mesa en El Golfo.";
    st.add(text);
    auto *check = new Note;
    check->type = Note::Check;
    check->title = "Compra de la semana";
    check->items = {{"Leche", false}, {"Pan integral de centeno con semillas", true},
                    {"Café molido", false}, {"Plátanos de Canarias", false}};
    st.add(check);
    auto *md = new Note;
    md->type = Note::Markdown;
    md->title = "Notas de la reunión";
    md->body = "# Reunión de producto\n\n- Builds móviles para iOS y Android\n- Revisar la "
               "interfaz en pantallas pequeñas\n\n**Siguiente paso:** capturas a 360 px.";
    st.add(md);

    auto *meeting = new Event;
    meeting->title = "Reunión de equipo con producto y diseño";
    meeting->date = today;
    meeting->start = QTime(10, 0);
    meeting->end = QTime(11, 30);
    st.addEvent(meeting, false);
    auto *task = new Event;
    task->title = "Enviar presupuesto";
    task->kind = Event::Task;
    task->date = today.addDays(1);
    task->start = QTime(16, 0);
    task->end = QTime(16, 30);
    st.addEvent(task, false);
    auto *trip = new Event;
    trip->title = "Viaje";
    trip->allDay = true;
    trip->date = today.addDays(2);
    trip->endDate = today.addDays(4);
    st.addEvent(trip, false);

    auto *ana = new Birthday;
    ana->name = "Ana García Hernández";
    ana->relation = "Hermana";
    ana->day = today.addDays(3).day();
    ana->month = today.addDays(3).month();
    ana->year = 1990;
    st.addBirthday(ana);
    auto *luis = new Birthday;
    luis->name = "Luis";
    luis->relation = "Trabajo";
    luis->day = today.addDays(40).day();
    luis->month = today.addDays(40).month();
    st.addBirthday(luis);

    auto *proj = new Project;
    proj->name = "Tagoror";
    proj->color = 1;
    st.addProject(proj);
    auto *done = new Session;
    done->task = "Builds móviles para iOS y Android";
    done->project = proj->id;
    done->startMs = now.addSecs(-3 * 3600).toMSecsSinceEpoch();
    done->endMs = now.addSecs(-3600).toMSecsSinceEpoch();
    st.addSession(done);

    p->createReminder("Llamar al dentista para la revisión", now.addSecs(3 * 3600));
    p->createTimer("Pasta", 9 * 60 * 1000);
    p->createTimer("Pomodoro", 25 * 60 * 1000);
    if (!st.timers().isEmpty()) p->toggleTimer(st.timers().first());
    p->startTracking("Revisar las capturas", proj->id);

    p->rebuildList();
    p->refreshPlanner();
    p->refreshBirthdays();
    p->onTimersChanged();
    p->refreshTracking();
    p->refreshNav();
}

void ScreenshotTour::closeEverything() {
    for (int i = 0; i < 12 && m_panel->handleBack(); ++i) {}
    m_panel->showNotes();
}

void ScreenshotTour::next() {
    if (m_at >= m_steps.size()) {
        QFile f(m_dir.filePath("report.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(m_report.toUtf8());
        qApp->quit();
        return;
    }
    const Step step = m_steps.at(m_at++);
    step.open();
    QTimer::singleShot(kSettleMs, this, [this, step] {
        capture(step.name);
        step.close();
        closeEverything();
        QTimer::singleShot(150, this, &ScreenshotTour::next);
    });
}

/// The window plus whatever floats over it (menus, pickers), each drawn where
/// it is on screen.
void ScreenshotTour::capture(const QString &name) {
    // What the phone shows: the screen, not the widget, which may be larger.
    const QRect screen = m_panel->screen() ? m_panel->screen()->geometry() : m_panel->geometry();
    QPixmap shot(screen.size() * m_panel->devicePixelRatio());
    shot.setDevicePixelRatio(m_panel->devicePixelRatio());
    shot.fill(Qt::black);
    {
        QPainter painter(&shot);
        painter.drawPixmap(m_panel->geometry().topLeft() - screen.topLeft(), m_panel->grab());
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (w == m_panel || !w->isVisible()) continue;
            painter.drawPixmap(w->geometry().topLeft() - screen.topLeft(), w->grab());
        }
    }
    shot.save(m_dir.filePath(name + ".png"));

    QTextStream out(&m_report);
    out << "== " << name << QString(" (window %1x%2 on a %3x%4 screen)\n")
                                 .arg(m_panel->width()).arg(m_panel->height())
                                 .arg(screen.width()).arg(screen.height());
    // Larger than the screen: on a phone the rest is cut.
    if (m_panel->width() > screen.width() || m_panel->height() > screen.height())
        out << QString("  WINDOW LARGER THAN THE SCREEN (layout minimum %1x%2)\n")
                   .arg(m_panel->minimumWidth()).arg(m_panel->minimumHeight());
    report(name, m_panel, out);
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w != m_panel && w->isVisible()) {
            out << "  [floating " << typeName(w) << QString(" at %1,%2 %3x%4]\n")
                       .arg(w->x()).arg(w->y()).arg(w->width()).arg(w->height());
            report(name, w, out);
        }
    out << "\n";
}

void ScreenshotTour::report(const QString &, QWidget *root, QTextStream &out) const {
    // Everything is measured against the phone's screen, which for a floating
    // window is not its own width.
    const int screenW = m_panel->screen() ? m_panel->screen()->geometry().width() : m_panel->width();
    const QPoint origin = m_panel->mapToGlobal(QPoint(0, 0));
    const auto rectOf = [origin](const QWidget *w) {
        return QRect(w->mapToGlobal(QPoint(0, 0)) - origin, w->size());
    };
    const auto overflows = [&](const QWidget *w) {
        return w->isVisible() && rectOf(w).right() >= screenW + 1;
    };

    // The navigation bar must sit on the screen's bottom edge.
    const int screenH = m_panel->screen() ? m_panel->screen()->geometry().height() : m_panel->height();
    if (root == m_panel && m_panel->m_navBar && m_panel->m_navBar->isVisible()) {
        const QRect nav = rectOf(m_panel->m_navBar);
        out << QString("  nav %1: y=%2..%3 (screen bottom %4)%5\n")
                   .arg(m_panel->m_navBar->layoutMode() == NavBar::Rail ? "rail" : "bar")
                   .arg(nav.top()).arg(nav.bottom() + 1).arg(screenH)
                   .arg(m_panel->m_navBar->layoutMode() == NavBar::Rail ||
                                nav.bottom() + 1 != screenH
                            ? "  <-- NOT AT THE BOTTOM"
                            : "");
    }

    // Overflow: each outermost widget that passes the right edge, then the chain
    // down through whichever child reaches furthest right.
    int chains = 0;
    for (QWidget *w : root->findChildren<QWidget *>()) {
        // Menus and pickers are windows of their own, reported apart (and their
        // shadow margin may hang off the screen on purpose).
        if (w->isWindow() || !overflows(w) || !w->isVisibleTo(root)) continue;
        if (w->parentWidget() && w->parentWidget() != root && overflows(w->parentWidget())) continue;
        if (++chains > 6) break;
        out << "  OVERFLOW " << describe(w, rectOf(w)) << "\n";
        const QWidget *cur = w;
        for (int depth = 1; depth < 30; ++depth) {
            const QWidget *best = nullptr;
            for (QObject *o : cur->children()) {
                auto *c = qobject_cast<QWidget *>(o);
                if (!c || !c->isVisible() || c->isWindow()) continue;
                if (!best || rectOf(c).right() > rectOf(best).right()) best = c;
            }
            if (!best || rectOf(best).right() < screenW) break;
            out << "  " << QString(depth * 2, ' ') << describe(best, rectOf(best)) << "\n";
            cur = best;
        }
    }

    // Touch targets.
    QStringList small;
    for (QAbstractButton *b : root->findChildren<QAbstractButton *>())
        if (b->isVisible() && b->height() < kMinTouch)
            small << QString("%1(%2x%3)")
                         .arg(b->text().isEmpty() ? b->toolTip().left(24) : b->text().left(24))
                         .arg(b->width()).arg(b->height());
    if (!small.isEmpty())
        out << QString("  small touch targets: %1 — %2\n").arg(small.size()).arg(small.mid(0, 12).join(", "));
}
