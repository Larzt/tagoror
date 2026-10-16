#include "ui/settings.hpp"

#include "audio/recorder.hpp"
#include "core/calendarsync.hpp"
#include "core/drivesync.hpp"
#include "core/folderbackend.hpp"
#include "core/syncengine.hpp"
#include "core/webdavbackend.hpp"
#include "core/lang.hpp"
#include "core/updater.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"

#include <QApplication>
#include <QAudioDevice>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// Paths are long and the row elides them; under home they are shown with ~
/// so the part that matters stays readable.
QString prettyPath(const QString &path) {
    const QString home = QDir::homePath();
    return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

/// An accent colour dot. Holds a pointer to the view's Theme, not a copy, so
/// changing the accent only needs a repaint (see SettingsView::setTheme()).
class ColorDot : public QWidget {
public:
    ColorDot(const QColor &color, const Theme *theme, std::function<void()> onClick,
             QWidget *parent = nullptr)
        : QWidget(parent), m_color(color), m_theme(theme), m_click(std::move(onClick)) {
        // 26, not 30: the accent row is the widest thing on the page, and a larger
        // system font grows the button next to the dots (see *Card widths*).
        setFixedSize(26, 26);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(color.name());
        keynav::activatable(this, [this] { if (m_click) m_click(); });
    }

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true;  update(); }
    void leaveEvent(QEvent *) override      { m_hover = false; update(); }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && m_click) m_click();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width() / 2.0, height() / 2.0);
        const bool current = m_theme && m_theme->accent.rgb() == m_color.rgb();

        // With keyboard focus the ring is in the text colour: the dot's own colour
        // already means "chosen" and cannot say both things.
        if (keynav::showsFocus(this)) {
            p.setPen(QPen(QColor(Theme::fg()), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 12.2, 12.2);
        } else if (current || m_hover) {
            p.setPen(QPen(current ? m_color : QColor(255, 255, 255, 60), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, 11.4, 11.4);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawRoundedRect(QRectF(c.x() - 8, c.y() - 8, 16, 16), 5.5, 5.5);
    }

private:
    QColor m_color;
    const Theme *m_theme;
    std::function<void()> m_click;
    bool m_hover = false;
};

/// A sliding switch, painted by hand. A checkbox says "tick this one from a
/// list"; a switch says "this is on or off", which is what these settings are.
class Switch : public QWidget {
public:
    Switch(bool on, const Theme *theme, std::function<void(bool)> changed,
           QWidget *parent = nullptr)
        : QWidget(parent), m_on(on), m_theme(theme), m_changed(std::move(changed)) {
        setFixedSize(38, 21);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { flip(); });
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint())) return;
        flip();
    }

    void flip() {
        m_on = !m_on;
        update();
        if (m_changed) m_changed(m_on);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QColor accent = m_theme ? m_theme->accent : QColor("#7c9cff");
        p.setPen(keynav::showsFocus(this) ? QPen(QColor(Theme::fg()), 1.5) : QPen(Qt::NoPen));
        p.setBrush(m_on ? accent : QColor(255, 255, 255, 28));
        const QRectF track = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
        p.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
        p.setPen(Qt::NoPen);

        const qreal r = height() / 2.0 - 3.0;
        const qreal x = m_on ? width() - height() / 2.0 : height() / 2.0;
        p.setBrush(m_on ? QColor("#0d1014") : QColor(Theme::muted()));
        p.drawEllipse(QPointF(x, height() / 2.0), r, r);
    }

private:
    bool m_on;
    const Theme *m_theme;
    std::function<void(bool)> m_changed;
};

/// A group row, with its own background on hover.
class SettingRow : public QWidget {
public:
    explicit SettingRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("setRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
    }

    // Only rows that do something when clicked (the microphones); toggle rows
    // leave the focus to the switch itself.
    void setClick(std::function<void()> f) {
        click = std::move(f);
        keynav::activatable(this, [this] { if (click) click(); });
    }

    std::function<void()> click;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && click)
            click();
    }
};

/// An icon painted in the accent. Not a QLabel with a pixmap, so an accent
/// change only needs a repaint (see setTheme()).
class AccentIcon : public QWidget {
public:
    AccentIcon(const QString &kind, const Theme *theme, int px, QWidget *parent = nullptr)
        : QWidget(parent), m_kind(kind), m_theme(theme), m_px(px) {
        setFixedSize(px, px);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_kind.isEmpty() || !m_theme) return;
        QPainter p(this);
        p.drawPixmap(0, 0, paintIcon(m_kind, m_theme->accent, m_px).pixmap(m_px, m_px));
    }

private:
    QString m_kind;
    const Theme *m_theme;
    int m_px;
};

/// Title and subtitle stacked, both elided: see *Card widths*.
QVBoxLayout *textColumn(const QString &title, const QString &hint, ElidedLabel **subOut = nullptr) {
    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(1);
    auto *name = new ElidedLabel(title, QColor(Theme::fg()));
    name->setObjectName("setRowText");
    texts->addWidget(name);
    if (!hint.isEmpty() || subOut) {
        auto *sub = new ElidedLabel(hint, QColor(Theme::muted()));
        sub->setObjectName("meta");
        sub->setToolTip(hint);
        texts->addWidget(sub);
        if (subOut) *subOut = sub;
    }
    return texts;
}

QToolButton *actionButton(const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName("segButton");
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

}  // namespace

SettingsView::SettingsView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("settings");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 4, 9, 8);
    col->setSpacing(6);

    // The tabs, in a strip that scrolls sideways when the panel is narrow rather
    // than demanding the width of all five names (see *Card widths*).
    auto *tabs = new QWidget;
    auto *tl = new QHBoxLayout(tabs);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(2);
    const QList<QPair<Tab, const char *>> names{{General, "General"},
                                                {Appearance, "Apariencia"},
                                                {Sync, "Sincronización"},
                                                {Shortcuts, "Atajos"},
                                                {About, "Acerca de"}};
    for (const auto &[tab, name] : names) {
        auto *b = new QToolButton;
        b->setObjectName("setTab");
        b->setProperty("tip", name);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        connect(b, &QToolButton::clicked, this, [this, tab = tab] { setTab(tab); });
        tl->addWidget(b);
        m_tabButtons << b;
    }
    tl->addStretch();
    auto *tabScroll = new QScrollArea;
    tabScroll->setObjectName("setTabs");
    tabScroll->setWidget(tabs);
    tabScroll->setWidgetResizable(true);
    tabScroll->setFrameShape(QFrame::NoFrame);
    tabScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScroll->setFixedHeight(32);
    tabScroll->viewport()->setAutoFillBackground(false);
    tabScroll->viewport()->setObjectName("scrollViewport");
    tabs->setObjectName("setTabsHost");
    col->addWidget(tabScroll);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addStretch();

    // Everything scrolls, as in birthdays, so the page's minimum height is a
    // constant that does not depend on how many microphones are plugged in.
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

void SettingsView::setSource(const Store *store) {
    m_store = store;
    refresh();
}

void SettingsView::setTheme(const Theme &theme) {
    m_theme = theme;
    // Stylesheet colours cascade from the Panel; only what is painted by hand and
    // reads the accent needs a repaint.
    for (QWidget *w : findChildren<QWidget *>()) w->update();
}

void SettingsView::setTab(Tab tab) {
    m_tab = tab;
    refresh();
}

void SettingsView::refresh() {
    m_updateSub = nullptr;   // whatever was there dies in this same sweep
    m_updateBtn = nullptr;
    m_syncStatus = nullptr;
    m_syncBtn = nullptr;
    m_group = nullptr;
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();          // see *Removing rows*: taking it out of the layout does not delete it
            w->deleteLater();
        }
        delete item;
    }
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        QToolButton *b = m_tabButtons.at(i);
        b->setText(L(b->property("tip").toString()));
        b->setProperty("chosen", i == m_tab);
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    if (!m_store) return;

    switch (m_tab) {
        case General:
            addLanguage();
            addWindow();
            addData();
            addInputs();
            addUpdates();
            addQuit();
            break;
        case Appearance:
            addAppearance();
            addAlarm();
            break;
        case Sync:
            // Recorded before building: refreshSync() compares against it and would
            // rebuild forever if it were taken afterwards.
            m_syncBuilt = syncSignature();
            addSync();
            addCalendar();
            break;
        case Shortcuts:
            addShortcuts();
            break;
        case About:
            addAbout();
            break;
    }
}

/// Every section is a header (accent icon plus name) and a group whose rows
/// are separated by a thin divider.
void SettingsView::beginGroup(const QString &title, const QString &icon) {
    auto *head = new QWidget;
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(3, m_layout->count() > 1 ? 16 : 8, 0, 6);
    hl->setSpacing(7);
    hl->addWidget(new AccentIcon(icon, &m_theme, 13), 0, Qt::AlignVCenter);
    auto *label = new QLabel(title);
    label->setObjectName("setHead");
    hl->addWidget(label, 1, Qt::AlignVCenter);
    m_layout->insertWidget(m_layout->count() - 1, head);

    m_group = new QFrame;
    m_group->setObjectName("setGroup");
    m_groupLayout = new QVBoxLayout(m_group);
    m_groupLayout->setContentsMargins(3, 3, 3, 3);
    m_groupLayout->setSpacing(0);
    m_layout->insertWidget(m_layout->count() - 1, m_group);
}

void SettingsView::addToGroup(QWidget *row) {
    if (m_groupLayout->count() > 0) {
        auto *rule = new QFrame;
        rule->setObjectName("setDivider");
        rule->setFixedHeight(1);
        auto *wrap = new QWidget;
        auto *wl = new QHBoxLayout(wrap);
        wl->setContentsMargins(8, 2, 8, 2);
        wl->addWidget(rule);
        m_groupLayout->addWidget(wrap);
    }
    m_groupLayout->addWidget(row);
}

/// A group row without hover background: rows holding their own controls
/// (dots, slider, segments) are not clickable themselves.
QVBoxLayout *SettingsView::addBlock() {
    auto *block = new QWidget;
    auto *l = new QVBoxLayout(block);
    l->setContentsMargins(8, 7, 8, 8);
    l->setSpacing(7);
    addToGroup(block);
    return l;
}

void SettingsView::addAppearance() {
    beginGroup(L("APARIENCIA"), "palette");

    // Accent
    {
        QVBoxLayout *b = addBlock();
        b->addLayout(textColumn(L("Color de acento"), QString()));

        auto *l = new QHBoxLayout;
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(2);
        const QList<QColor> swatches = {QColor("#7c9cff"), QColor("#6fcf97"), QColor("#f2b757"),
                                        QColor("#ff7a6b"), QColor("#b98cff"), QColor("#4ecdc4")};
        for (const QColor &c : swatches)
            l->addWidget(new ColorDot(c, &m_theme, [this, c] { emit accentPicked(c); }));
        l->addStretch();

        auto *custom = actionButton(L("Otro…"));
        custom->setToolTip(L("Color personalizado…"));
        connect(custom, &QToolButton::clicked, this,
                [this, custom] { emit accentEditorRequested(custom); });
        l->addWidget(custom, 0, Qt::AlignVCenter);
        b->addLayout(l);
    }

    // Opacity
    {
        QVBoxLayout *b = addBlock();
        auto *hl = new QHBoxLayout;
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(6);
        hl->addLayout(textColumn(L("Opacidad"), QString()), 1);
        auto *readout = new QLabel(QString("%1%").arg(m_theme.opacity));
        readout->setObjectName("setValue");
        readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(readout);
        b->addLayout(hl);

        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(40, 100);
        slider->setValue(m_theme.opacity);
        connect(slider, &QSlider::valueChanged, this, [this, readout](int v) {
            readout->setText(QString("%1%").arg(v));
            emit opacityChanged(v);
        });
        b->addWidget(slider);
    }

    // Typeface: the name of the one in use, and a menu to pick another. Above the
    // text size, whose sample card shows it too.
    addAction(L("Tipografía"), fontDisplayName(m_store->prefs().font), L("Cambiar…"), true,
              [this](QWidget *anchor) { emit fontEditorRequested(anchor); });

    // Text size: four sizes rather than a slider, each showing its "Aa" at the
    // size it gives, so the row explains itself in both languages without words
    // that would not fit (see *Card widths*).
    {
        QVBoxLayout *b = addBlock();
        b->addLayout(textColumn(L("Tamaño de texto"), QString()));

        struct Level { int percent; const char *name; };
        static const Level levels[] = {{88, "Pequeño"}, {100, "Normal"},
                                       {114, "Grande"}, {130, "Muy grande"}};
        const int current = m_store->prefs().textScale;
        QStringList labels;
        int chosen = 1;
        for (int i = 0; i < 4; ++i) {
            labels << "Aa";
            if (levels[i].percent == current) chosen = i;
        }
        QList<QToolButton *> buttons;
        b->addWidget(segments(labels, chosen, [this](int i) {
            static const int pct[] = {88, 100, 114, 130};
            emit textScalePicked(pct[i]);
        }, &buttons));
        for (int i = 0; i < buttons.size(); ++i) {
            buttons[i]->setToolTip(L(levels[i].name));
            buttons[i]->setAccessibleName(L(levels[i].name));
            // With a selector: a bare rule would cascade to everything under the button.
            buttons[i]->setStyleSheet(QString("QToolButton#segOption { font-size: %1px; }")
                                          .arg(11.0 * levels[i].percent / 100.0, 0, 'f', 1));
        }

        // A sample using a card's object names, so it scales with the stylesheet like
        // real notes.
        auto *card = new QFrame;
        card->setObjectName("setCard");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(10, 8, 10, 9);
        cl->setSpacing(3);
        auto *title = new QLabel(L("Comprar pan y café"));
        title->setObjectName("cardTitle");
        title->setWordWrap(true);
        title->setMinimumWidth(24);   // see *Card widths*
        auto *body = new QLabel(L("Así se verán las notas, las listas y el planificador."));
        body->setObjectName("body");
        body->setWordWrap(true);
        body->setMinimumWidth(24);
        cl->addWidget(title);
        cl->addWidget(body);
        b->addWidget(card);
    }

    // Density: how much room the cards take. Two words fit side by side.
    {
        QVBoxLayout *b = addBlock();
        b->addLayout(textColumn(L("Densidad"), L("Compacta deja ver más notas a la vez")));
        b->addWidget(segments({L("Normal"), L("Compacta")}, m_store->prefs().compact ? 1 : 0,
                              [this](int i) { emit compactPicked(i == 1); }));
    }
}

QString alarmToneName(const QString &id) {
    if (id == "bell") return L("Campana");
    if (id == "digital") return L("Digital");
    if (id == "rise") return L("Ascendente");
    if (id == "custom") return L("Archivo propio");
    return L("Dos pitidos");
}

/// The tone and its volume. A menu rather than segments: five names do not fit
/// side by side (see *Card widths*).
void SettingsView::addAlarm() {
    beginGroup(L("ALARMA"), "bell");
    addAction(L("Tono"), alarmToneName(m_store->prefs().alarmTone), L("Cambiar…"), true,
              [this](QWidget *anchor) { emit alarmToneRequested(anchor); });

    QVBoxLayout *b = addBlock();
    auto *hl = new QHBoxLayout;
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);
    hl->addLayout(textColumn(L("Volumen"), QString()), 1);
    auto *readout = new QLabel(QString("%1%").arg(m_store->prefs().alarmVolume));
    readout->setObjectName("setValue");
    readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hl->addWidget(readout);
    auto *test = actionButton(L("Probar"));
    test->setToolTip(L("Escuchar el tono"));
    connect(test, &QToolButton::clicked, this, [this] { emit alarmPreviewRequested(); });
    hl->addWidget(test, 0, Qt::AlignVCenter);
    b->addLayout(hl);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(m_store->prefs().alarmVolume);
    connect(slider, &QSlider::valueChanged, this, [this, readout](int v) {
        readout->setText(QString("%1%").arg(v));
        emit alarmVolumeChanged(v);
    });
    // Letting go plays it, so the level is heard and not guessed.
    connect(slider, &QSlider::sliderReleased, this, [this] { emit alarmPreviewRequested(); });
    b->addWidget(slider);
}

QString fontDisplayName(const QString &pref) {
    if (pref.isEmpty()) return QStringLiteral("IBM Plex Sans");
    if (pref == "system") return L("La del sistema");
    return pref;
}

void SettingsView::addLanguage() {
    beginGroup(L("IDIOMA"), "globe");
    // Each name in its own language, untranslated: someone who opened this in the
    // language they do not understand must recognise the other.
    addBlock()->addWidget(segments({"Español", "English"},
                                   m_store->prefs().lang == Lang::Es ? 0 : 1,
                                   [this](int i) {
                                       emit languagePicked(i == 0 ? Lang::Es : Lang::En);
                                   }));
}

void SettingsView::addWindow() {
    beginGroup(L("VENTANA"), "window");
    addToggle(L("Modo aplicación"),
              m_store->prefs().appMode ? L("Ventana normal, en la barra de tareas")
                                       : L("Widget sin marco en el escritorio"),
              m_store->prefs().appMode, [this](bool on) { emit appModeToggled(on); });
    addToggle(L("Siempre encima"),
              m_store->prefs().onTop     ? L("Por encima de todo")
              : m_store->prefs().appMode ? L("Como cualquier otra ventana")
                                         : L("Pegada al escritorio"),
              m_store->prefs().onTop, [this](bool on) { emit onTopToggled(on); });
    // The widget's thing: a normal window keeps whatever size it is given.
    if (!m_store->prefs().appMode)
        addToggle(L("Tamaño por página"),
                  m_store->prefs().sizePerPage ? L("Cada página recuerda el suyo")
                                               : L("El mismo para todas"),
                  m_store->prefs().sizePerPage, [this](bool on) { emit sizePerPageToggled(on); });

#ifdef Q_OS_LINUX
    // See choosePlatform() in main.cpp: the panel's placement and "always on top"
    // depend on this. Outside Linux the choice does not exist.
    const bool nativeWayland = QSettings().value("platform").toString() == "wayland";
    addToggle(L("Compatibilidad X11"),
              nativeWayland ? L("Desactivada · Wayland nativo")
                            : L("Activada · %1").arg(qApp->platformName()),
              !nativeWayland, [this](bool on) { emit x11Toggled(on); });
#endif
}

void SettingsView::addData() {
    beginGroup(L("DATOS"), "folder");
    addAction(L("Carpeta de guardado"),
              m_store->available() ? prettyPath(appDataDir())
                                   : L("No disponible · %1").arg(prettyPath(appDataDir())),
              L("Cambiar"), true, [this](QWidget *) { emit dataFolderRequested(); });

    const Store::Prefs &prefs = m_store->prefs();
    const int kept = int(m_store->backups().size());
    QString sub = prefs.backupEveryDays == 0
                      ? L("Solo a mano")
                      : L("Cada %1 días").arg(prefs.backupEveryDays);
    if (prefs.backupEveryDays == 1) sub = L("Cada día");
    if (prefs.backupEveryDays == 7) sub = L("Cada semana");
    if (prefs.backupEveryDays == 30) sub = L("Cada mes");
    sub += kept == 0 ? " · " + L("ninguna guardada") : " · " + L("%1 guardadas").arg(kept);

    addAction(L("Copias de seguridad"), sub, L("Gestionar"), m_store->available(),
              [this](QWidget *anchor) { emit backupsRequested(anchor); });
}

void SettingsView::setSync(const SyncController *sync) {
    m_sync = sync;
    refresh();
}

/// What the sync tab is built from. When it changes the tab is rebuilt; the
/// rest (a pass starting or ending) only rewrites the status line.
QString SettingsView::syncSignature() const {
    if (!m_sync) return QString();
    const DriveSync *g = m_sync->drive();
    QStringList parts{m_sync->provider(), QString::number(m_sync->when()),
                      QString::number(g->state()), g->account(), g->lastError(),
                      m_sync->webdav()->server(), m_sync->webdav()->user(),
                      m_sync->webdav()->ready() ? "ready" : "", m_sync->folder()->path(),
                      m_sync->folder()->error()};
    const CalendarSync *cal = g->calendar();
    parts << (cal->enabled() ? "on" : "off") << (g->hasCalendarScope() ? "scope" : "")
          << cal->target() << cal->lastError();
    for (const CalendarSync::Calendar &c : cal->calendars())
        parts << c.id + (cal->isFollowed(c.id) ? "+" : "-") + c.name;
    return parts.join('\n');
}

namespace {
/// "hace 2 min", "hace 3 h", or the date: how long since the last pass.
QString ago(const QDateTime &when) {
    const qint64 s = when.secsTo(QDateTime::currentDateTime());
    if (s < 60) return L("hace un momento");
    if (s < 3600) return L("hace %1 min").arg(s / 60);
    if (s < 86400) return L("hace %1 h").arg(s / 3600);
    return Lang::locale().toString(when, "d MMM · HH:mm");
}
}  // namespace

QString SettingsView::syncStatus(bool *bad) const {
    *bad = false;
    const SyncEngine *e = m_sync->engine();
    const SyncBackend *b = e->backend();
    if (!b) return L("Sin sincronizar · las notas se quedan en este equipo");
    if (!b->ready()) return L("Sin configurar todavía");
    if (e->state() == SyncEngine::Syncing) return L("Sincronizando…");
    if (e->state() == SyncEngine::Failed) {
        *bad = true;
        return e->lastError();
    }
    return e->lastSync().isValid() ? L("Sincronizado %1").arg(ago(e->lastSync()))
                                   : L("Sin sincronizar todavía");
}

void SettingsView::refreshSync() {
    if (!m_sync || m_tab != Sync) return;
    if (syncSignature() != m_syncBuilt) {
        // Never under the cursor: a field of this tab being typed in would lose it.
        QWidget *f = QApplication::focusWidget();
        if (!(f && isAncestorOf(f) && qobject_cast<QLineEdit *>(f))) {
            refresh();
            return;
        }
    }
    if (!m_syncStatus || !m_syncBtn) return;
    bool bad = false;
    const QString text = syncStatus(&bad);
    m_syncStatus->setText(text);
    m_syncStatus->setToolTip(text);
    m_syncStatus->setColor(bad ? QColor("#ff7a6b") : QColor(Theme::muted()));
    const SyncBackend *b = m_sync->engine()->backend();
    m_syncBtn->setEnabled(b && b->ready() && m_sync->engine()->state() != SyncEngine::Syncing);
}

/// Where the shared copy of the notes, areas and timers lives: one provider of
/// three, each card showing its own fields only while chosen.
void SettingsView::addSync() {
    if (!m_sync) return;
    beginGroup(L("SINCRONIZAR NOTAS"), "cloud");
    auto *intro = new QLabel(L("Dónde se guarda la copia de tus notas, áreas y timers para tus otros equipos."));
    intro->setObjectName("meta");
    intro->setWordWrap(true);
    intro->setMinimumWidth(24);   // see *Card widths*
    intro->setContentsMargins(8, 6, 8, 6);
    addToGroup(intro);

    const QString p = m_sync->provider();
    addProvider("", "minus", L("Ninguno"), L("Las notas se quedan en este equipo"), p.isEmpty());
    addProvider("drive", "cloud", QStringLiteral("Google Drive"),
                L("Carpeta privada de la app en tu Drive"), p == "drive");
    addProvider("webdav", "folder", QStringLiteral("WebDAV / Nextcloud"),
                L("Tu servidor; mejor con una contraseña de aplicación"), p == "webdav");
    addProvider("folder", "folder", L("Carpeta sincronizada"),
                L("Syncthing, Dropbox, OneDrive… Tagoror solo escribe en ella"), p == "folder");

    // When, for whichever provider.
    QVBoxLayout *b = addBlock();
    b->addLayout(textColumn(L("Cuándo sincronizar"),
                            L("Al guardar también recoge los cambios de los otros equipos")));
    b->addWidget(segments({L("Al guardar"), L("Cada 5 min"), L("A mano")}, m_sync->when(),
                          [this](int i) { emit syncWhenPicked(i); }));
}

void SettingsView::addProvider(const QString &id, const QString &icon, const QString &title,
                               const QString &hint, bool chosen) {
    auto *card = new SettingRow;
    card->setObjectName("provCard");
    card->setProperty("chosen", chosen);
    card->setCursor(chosen ? Qt::ArrowCursor : Qt::PointingHandCursor);
    if (!chosen) card->setClick([this, id] { emit syncProviderPicked(id); });
    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(7);

    auto *head = new QHBoxLayout;
    head->setSpacing(9);
    auto *radio = new QLabel;
    radio->setFixedSize(16, 16);
    {
        QPixmap pm(32, 32);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter rp(&pm);
        rp.setRenderHint(QPainter::Antialiasing);
        rp.setPen(QPen(chosen ? m_theme.accent : QColor(Theme::muted()), 1.5));
        rp.setBrush(Qt::NoBrush);
        rp.drawEllipse(QPointF(8, 8), 6.5, 6.5);
        if (chosen) {
            rp.setPen(Qt::NoPen);
            rp.setBrush(m_theme.accent);
            rp.drawEllipse(QPointF(8, 8), 3.5, 3.5);
        }
        rp.end();
        radio->setPixmap(pm);
    }
    head->addWidget(radio, 0, Qt::AlignTop);
    head->addLayout(textColumn(title, hint), 1);
    head->addWidget(new AccentIcon(chosen ? icon : QString(), &m_theme, 14), 0, Qt::AlignTop);
    col->addLayout(head);
    addToGroup(card);
    if (!chosen) return;

    const auto field = [](const QString &placeholder, const QString &text, bool secret) {
        auto *e = new QLineEdit(text);
        e->setObjectName("setField");
        e->setPlaceholderText(placeholder);
        e->setMinimumWidth(24);   // see *Card widths*
        e->setContextMenuPolicy(Qt::NoContextMenu);
        if (secret) e->setEchoMode(QLineEdit::Password);
        return e;
    };
    const auto actionRow = [this, col](const QString &label, bool enabled,
                                       std::function<void()> clicked) {
        auto *row = new QHBoxLayout;
        row->addStretch();
        auto *b = actionButton(label);
        b->setEnabled(enabled);
        connect(b, &QToolButton::clicked, this, [clicked] { clicked(); });
        row->addWidget(b);
        col->addLayout(row);
        return b;
    };

    if (id == "drive") {
        const DriveSync *g = m_sync->drive();
        QString line;
        switch (g->state()) {
            case DriveSync::Unavailable: line = L("No incluida en esta compilación"); break;
            case DriveSync::Authorizing: line = L("Autoriza el acceso en el navegador…"); break;
            case DriveSync::Disconnected:
                line = g->lastError().isEmpty() ? L("Sin conectar") : g->lastError();
                break;
            default: line = g->account().isEmpty() ? L("Conectado") : g->account(); break;
        }
        auto *account = new ElidedLabel(line, QColor(g->lastError().isEmpty() ? Theme::muted()
                                                                              : "#ff7a6b"));
        account->setObjectName("meta");
        account->setToolTip(line);
        col->addWidget(account);
        if (g->state() == DriveSync::Idle)
            actionRow(L("Desconectar"), true, [this] { emit googleDisconnectRequested(); });
        else if (g->state() == DriveSync::Authorizing)
            actionRow(L("Cancelar"), true, [this] { emit googleCancelRequested(); });
        else
            actionRow(L("Conectar"), g->state() != DriveSync::Unavailable,
                      [this] { emit googleConnectRequested(); });
    } else if (id == "webdav") {
        const WebDavBackend *w = m_sync->webdav();
        auto *srvLabel = new QLabel(L("Servidor"));
        srvLabel->setObjectName("setFieldLabel");
        col->addWidget(srvLabel);
        auto *server = field(QStringLiteral("https://nube.ejemplo.es/remote.php/dav/files/ana"),
                             w->server(), false);
        col->addWidget(server);
        auto *pair = new QHBoxLayout;
        pair->setSpacing(6);
        auto *userCol = new QVBoxLayout;
        auto *userLabel = new QLabel(L("Usuario"));
        userLabel->setObjectName("setFieldLabel");
        userCol->addWidget(userLabel);
        auto *user = field(QString(), w->user(), false);
        userCol->addWidget(user);
        auto *passCol = new QVBoxLayout;
        auto *passLabel = new QLabel(L("Contraseña de app"));
        passLabel->setObjectName("setFieldLabel");
        passCol->addWidget(passLabel);
        // Never shown back: an empty field on save keeps the stored one.
        auto *pass = field(w->hasPassword() ? L("guardada") : QString(), QString(), true);
        passCol->addWidget(pass);
        pair->addLayout(userCol, 1);
        pair->addLayout(passCol, 1);
        col->addLayout(pair);
        auto *buttons = new QHBoxLayout;
        if (w->ready()) {
            auto *forget = actionButton(L("Olvidar"));
            connect(forget, &QToolButton::clicked, this, [this] { emit webdavForgetRequested(); });
            buttons->addWidget(forget);
        }
        buttons->addStretch();
        auto *save = actionButton(L("Guardar"));
        save->setProperty("chosen", true);
        const auto submit = [this, server, user, pass] {
            emit webdavSaveRequested(server->text(), user->text(), pass->text());
        };
        connect(save, &QToolButton::clicked, this, submit);
        for (QLineEdit *e : {server, user, pass}) connect(e, &QLineEdit::returnPressed, this, submit);
        buttons->addWidget(save);
        col->addLayout(buttons);
    } else if (id == "folder") {
        const FolderBackend *f = m_sync->folder();
        const QString shown = f->path().isEmpty() ? L("Ninguna elegida") : prettyPath(f->path());
        const bool bad = !f->error().isEmpty() || (!f->path().isEmpty() && !f->ready());
        const QString line = !f->error().isEmpty() ? f->error()
                             : bad                 ? L("No disponible · %1").arg(shown)
                                                   : shown;
        auto *path = new ElidedLabel(line, QColor(bad ? "#ff7a6b" : Theme::muted()));
        path->setObjectName("meta");
        path->setToolTip(f->path());
        col->addWidget(path);
        actionRow(f->path().isEmpty() ? L("Elegir…") : L("Cambiar…"), true,
                  [this] { emit folderChooseRequested(); });
    }

    if (id.isEmpty()) return;
    // The state and the button to sync now, for the chosen provider.
    auto *status = new QHBoxLayout;
    status->setSpacing(6);
    auto *dot = new AccentIcon(QStringLiteral("glyph-dot"), &m_theme, 8);
    status->addWidget(dot, 0, Qt::AlignVCenter);
    m_syncStatus = new ElidedLabel(QString(), QColor(Theme::muted()));
    m_syncStatus->setObjectName("syncStatus");
    status->addWidget(m_syncStatus, 1);
    m_syncBtn = actionButton(L("Sincronizar ahora"));
    connect(m_syncBtn, &QToolButton::clicked, this, [this] { emit syncNowRequested(); });
    status->addWidget(m_syncBtn);
    col->addLayout(status);
    refreshSync();
}

/// Google Calendar: its own connection, whatever the notes sync with. It rides
/// the Google account; each followed calendar is a planner category.
void SettingsView::addCalendar() {
    if (!m_sync) return;
    const DriveSync *g = m_sync->drive();
    if (g->state() == DriveSync::Unavailable) return;
    const CalendarSync *cal = g->calendar();
    beginGroup(L("GOOGLE CALENDAR"), "calendar");

    addToggle(L("Sincronizar el planificador"),
              L("Eventos y tareas, en los dos sentidos"), cal->enabled(),
              [this](bool on) { emit calendarToggled(on); });
    if (!cal->enabled()) return;

    if (!g->connected()) {
        addAction(L("Cuenta de Google"),
                  g->state() == DriveSync::Authorizing ? L("Autoriza el acceso en el navegador…")
                                                       : L("Sin conectar"),
                  g->state() == DriveSync::Authorizing ? L("Cancelar") : L("Conectar"), true,
                  [this, g](QWidget *) {
                      if (g->state() == DriveSync::Authorizing) emit googleCancelRequested();
                      else emit googleConnectRequested();
                  });
        return;
    }
    if (!g->hasCalendarScope()) {
        addAction(L("Falta el permiso de Calendar"),
                  L("Vuelve a autorizar la cuenta para concederlo"), L("Autorizar"),
                  g->state() != DriveSync::Authorizing,
                  [this](QWidget *) { emit calendarGrantRequested(); });
        return;
    }
    if (cal->calendars().isEmpty()) {
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 6, 8, 7);
        auto *wait = new QLabel(L("Los calendarios de la cuenta aparecen tras la próxima sincronización."));
        wait->setObjectName("meta");
        wait->setWordWrap(true);
        wait->setMinimumWidth(24);   // see *Card widths*
        rl->addWidget(wait, 1);
        addToGroup(row);
        return;
    }

    for (const CalendarSync::Calendar &c : cal->calendars()) {
        QString hint = c.primary ? L("Principal") : QString();
        if (!c.writable) hint = hint.isEmpty() ? L("Solo lectura") : hint + " · " + L("Solo lectura");
        addToggle(c.name, hint, cal->isFollowed(c.id),
                  [this, id = c.id](bool on) { emit calendarFollowToggled(id, on); });
    }

    const CalendarSync::Calendar *target = cal->calendar(cal->target());
    const bool usable = target && target->writable && cal->isFollowed(target->id);
    addAction(L("Eventos nuevos de Tagoror"),
              usable ? L("Se guardan en «%1»").arg(target->name)
                     : L("En ninguno · se quedan en Tagoror"),
              L("Cambiar"), true, [this](QWidget *anchor) { emit calendarTargetRequested(anchor); });

    if (!cal->lastError().isEmpty()) {
        ElidedLabel *sub = nullptr;
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 4, 8, 6);
        rl->addLayout(textColumn(L("Último error"), cal->lastError(), &sub), 1);
        sub->setColor(QColor("#ff7a6b"));
        addToGroup(row);
    }
}

/// Every shortcut, as a reference: the palette shows them too, next to what
/// they do.
void SettingsView::addShortcuts() {
    const QList<QPair<const char *, QList<QPair<const char *, QString>>>> groups{
        {"PÁGINAS", {{"Notas", "Ctrl+1"}, {"Planner", "Ctrl+2"}, {"Timers", "Ctrl+3"},
                     {"Cumpleaños", "Ctrl+4"}, {"Ajustes", "Ctrl+,"},
                     {"Paleta de comandos", "Ctrl+K"}, {"Volver a las notas", "Esc"}}},
        {"NOTAS", {{"Nota de texto", "Ctrl+N"}, {"Checklist", "Ctrl+⇧L"},
                   {"Recordatorio", "Ctrl+⇧R"}, {"Nota de voz", "Ctrl+⇧V"},
                   {"Nota Markdown", "Ctrl+⇧M"}, {"Buscar", "Ctrl+F"},
                   {"Terminar de escribir", "Enter"}, {"Nueva línea en una nota", "⇧+Enter"}}},
        {"ÁREAS", {{"Nueva área", "Ctrl+T"}, {"Ir a un área", "Alt+1…9"},
                   {"Renombrar", "F2"}, {"Mover", "Ctrl+⇧+← →"}}},
        {"NOTA MARKDOWN", {{"Editar ⇄ vista previa", "Ctrl+E"}, {"Negrita", "Ctrl+B"},
                           {"Cursiva", "Ctrl+I"}, {"Volver", "Ctrl+Enter"}}},
        {"VENTANA", {{"Plegar a icono", "Ctrl+M"}}},
    };
    for (const auto &[title, rows] : groups) {
        beginGroup(L(title), "command");
        for (const auto &[what, keys] : rows) {
            auto *row = new QWidget;
            auto *l = new QHBoxLayout(row);
            l->setContentsMargins(8, 6, 8, 6);
            auto *name = new ElidedLabel(L(what), QColor(Theme::fg()));
            name->setObjectName("setRowText");
            l->addWidget(name, 1);
            auto *key = new QLabel(keys);
            key->setObjectName("paletteKey");
            l->addWidget(key);
            addToGroup(row);
        }
    }
}

void SettingsView::addAbout() {
    beginGroup(QStringLiteral("TAGOROR"), "info");
    QVBoxLayout *b = addBlock();
    auto *version = new QLabel(L("Versión %1").arg(Updater::current()));
    version->setObjectName("setRowText");
    b->addWidget(version);
    auto *text = new QLabel(L("Notas, recordatorios, planificador y temporizadores en un panel de "
                              "escritorio. Un tagoror es el círculo de piedras donde se reunía el "
                              "consejo guanche."));
    text->setObjectName("meta");
    text->setWordWrap(true);
    text->setMinimumWidth(24);
    b->addWidget(text);
    addAction(L("Código y novedades"), QStringLiteral("github.com/Larzt/tagoror"), L("Abrir"), true,
              [](QWidget *) { QDesktopServices::openUrl(QUrl("https://github.com/Larzt/tagoror")); });
    QVBoxLayout *lic = addBlock();
    auto *licText = new QLabel(L("Tagoror se publica con licencia MIT. Incluye IBM Plex Sans y IBM Plex Mono "
                                 "(SIL Open Font License 1.1) y usa Qt 6 bajo LGPL-3.0."));
    licText->setObjectName("meta");
    licText->setWordWrap(true);
    licText->setMinimumWidth(24);
    lic->addWidget(licText);
}

void SettingsView::addInputs() {
    beginGroup(L("MICRÓFONO"), "voice");

    const QAudioDevice current = QMediaDevices::defaultAudioInput();
    const QByteArray chosen = m_store->prefs().input;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();

    if (devices.isEmpty()) {
        auto *none = new QLabel(L("No hay micrófono disponible"));
        none->setObjectName("meta");
        none->setWordWrap(true);   // see *Card widths*: it must not demand its width
        none->setMinimumWidth(24);
        none->setContentsMargins(8, 7, 8, 7);
        addToGroup(none);
        return;
    }

    for (const QAudioDevice &dev : devices) {
        const bool inUse = chosen.isEmpty() ? dev.id() == current.id() : dev.id() == chosen;

        auto *row = new SettingRow;
        row->setCursor(Qt::PointingHandCursor);
        row->setClick([this, id = dev.id()] { emit inputPicked(id); });

        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(8, 7, 8, 7);
        l->setSpacing(8);

        auto *name = new ElidedLabel(dev.description().trimmed(),
                                     QColor(inUse ? Theme::fg() : Theme::muted()));
        name->setObjectName("setRowText");
        name->setToolTip(dev.description());
        l->addWidget(name, 1);
        // The tick on the right, as in system menus: on the left it indented the
        // unchosen rows for no visible reason.
        l->addWidget(new AccentIcon(inUse ? "check" : QString(), &m_theme, 13), 0,
                     Qt::AlignVCenter);
        addToGroup(row);
    }
}

/// What the update card says: checking, the error, a new version, or up to
/// date.
QString SettingsView::updateSubtitle(bool busy, const QString &error) const {
    if (busy) return L("Buscando…");
    if (!error.isEmpty()) return error;

    const QString latest = m_store->prefs().latestSeen;
    if (!latest.isEmpty() && Updater::compare(latest, Updater::current()) > 0)
        return L("%1 disponible").arg(latest);

    const qint64 last = m_store->prefs().lastUpdateMs;
    if (last <= 0) return L("Sin comprobar todavía");
    return L("Al día · %1").arg(
        Lang::locale().toString(QDateTime::fromMSecsSinceEpoch(last), "d MMM · HH:mm"));
}

void SettingsView::addUpdates() {
    beginGroup(L("ACTUALIZACIONES"), "download");

    const bool on = m_store->prefs().updateCheck;
    addToggle(L("Buscar automáticamente"),
              on ? L("Una vez al día") : L("Desactivado · solo a mano"), on,
              [this](bool v) { emit updateCheckToggled(v); });

    // A new version exists: the button opens it. Otherwise it checks.
    const QString latest = m_store->prefs().latestSeen;
    const bool hayNueva = !latest.isEmpty() &&
                          Updater::compare(latest, Updater::current()) > 0;

    m_updateBtn = addAction(QString("Tagoror %1").arg(Updater::current()),
                            updateSubtitle(m_updateBusy, m_updateError),
                            hayNueva ? L("Ver") : L("Buscar ahora"), !m_updateBusy,
                            [this, hayNueva](QWidget *) {
        if (hayNueva) emit openLatestRequested();
        else emit checkUpdatesRequested();
    }, &m_updateSub);
    m_updateBtn->setProperty("chosen", hayNueva);   // tinted when there is something new
    if (hayNueva) m_updateSub->setColor(m_theme.accent);
}

void SettingsView::setUpdateState(bool busy, const QString &error) {
    m_updateBusy = busy;
    m_updateError = error;
    if (!m_updateSub || !m_updateBtn) return;

    const QString sub = updateSubtitle(busy, error);
    m_updateSub->setText(sub);
    m_updateSub->setToolTip(sub);
    m_updateBtn->setEnabled(!busy);
}

void SettingsView::addQuit() {
    m_group = nullptr;
    auto *host = new QWidget;
    auto *l = new QHBoxLayout(host);
    l->setContentsMargins(0, 18, 0, 4);

    auto *quit = new QToolButton;
    quit->setObjectName("quitBtn");
    quit->setText(L("Salir de Tagoror"));
    quit->setIcon(paintIcon("power", QColor("#ff7a6b"), 13));
    quit->setIconSize(QSize(13, 13));
    quit->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    quit->setCursor(Qt::PointingHandCursor);
    connect(quit, &QToolButton::clicked, this, [this] { emit quitRequested(); });

    l->addStretch();
    l->addWidget(quit);
    l->addStretch();
    m_layout->insertWidget(m_layout->count() - 1, host);
}

void SettingsView::addToggle(const QString &label, const QString &hint, bool on,
                             std::function<void(bool)> changed) {
    auto *row = new SettingRow;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(8, 7, 8, 7);
    l->setSpacing(8);
    l->addLayout(textColumn(label, hint), 1);
    l->addWidget(new Switch(on, &m_theme, std::move(changed)), 0, Qt::AlignVCenter);
    addToGroup(row);
}

/// Segmented control: a track with the options side by side, sharing the
/// width equally, so they read as "one of these".
QWidget *SettingsView::segments(const QStringList &labels, int chosen,
                                std::function<void(int)> picked,
                                QList<QToolButton *> *out) {
    auto *track = new QFrame;
    track->setObjectName("segTrack");
    auto *l = new QHBoxLayout(track);
    l->setContentsMargins(3, 3, 3, 3);
    l->setSpacing(2);

    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QToolButton;
        b->setObjectName("segOption");
        b->setText(labels.at(i));
        b->setProperty("chosen", i == chosen);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QToolButton::clicked, this, [picked, i] { if (picked) picked(i); });
        l->addWidget(b, 1);
        if (out) out->append(b);
    }
    return track;
}

QToolButton *SettingsView::addAction(const QString &title, const QString &subtitle,
                                     const QString &action, bool enabled,
                                     std::function<void(QWidget *)> clicked,
                                     ElidedLabel **subOut) {
    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(8, 6, 6, 6);
    l->setSpacing(8);
    // The path is elided, not widening the row: the *Card widths* trap, and a
    // long path is exactly what triggers it.
    l->addLayout(textColumn(title, subtitle, subOut), 1);

    auto *btn = actionButton(action);
    btn->setEnabled(enabled);
    connect(btn, &QToolButton::clicked, this,
            [clicked, btn] { if (clicked) clicked(btn); });
    l->addWidget(btn, 0, Qt::AlignVCenter);
    addToGroup(row);
    return btn;
}
