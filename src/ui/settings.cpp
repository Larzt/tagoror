#include "ui/settings.hpp"

#include "audio/recorder.hpp"
#include "core/lang.hpp"
#include "core/updater.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"

#include <QApplication>
#include <QAudioDevice>
#include <QDateTime>
#include <QDir>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Las rutas son largas y la fila las corta por la mitad; bajo el home se
// enseñan con ~ para que se lea la parte que importa.
QString prettyPath(const QString &path) {
    const QString home = QDir::homePath();
    return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

// Punto de color del acento. Venía de Popup::addSwatches, que existía solo para
// este menú; al mudarse aquí, aquello se quedaba sin usar y se ha ido con él.
//
// Sostiene un puntero al Theme de la vista, no una copia: así cambiar el acento
// solo pide repintar, sin rehacer la página (ver SettingsView::setTheme).
class ColorDot : public QWidget {
public:
    ColorDot(const QColor &color, const Theme *theme, std::function<void()> onClick,
             QWidget *parent = nullptr)
        : QWidget(parent), m_color(color), m_theme(theme), m_click(std::move(onClick)) {
        // 26 y no 30: la fila del acento es lo más ancho de la página (los seis
        // puntos más el botón de al lado), y con una fuente de sistema mayor
        // ese botón crece. Ver *Card widths*: lo que aquí se pase de ancho
        // recorta la página entera, así que se deja holgura de sobra.
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

        // Con el foco del teclado, el anillo en el color del texto: el del
        // propio color ya dice "es el elegido" y no puede decir las dos cosas.
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

// Interruptor de los de deslizar. Se pinta a mano porque un QCheckBox con la
// hoja de estilos no llega a esto, y porque una casilla dice "marca esto de una
// lista" mientras que un interruptor dice "esto está encendido o apagado", que
// es justo lo que son "siempre encima" y la compatibilidad X11.
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

// Fila de un grupo, con fondo propio al pasar por encima.
class SettingRow : public QWidget {
public:
    explicit SettingRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("setRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
    }

    // Solo las filas que hacen algo al pulsarlas (los micrófonos); las de los
    // interruptores dejan el foco al propio interruptor.
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

// Icono pintado en el acento. No es un QLabel con un pixmap porque el acento
// cambia sin rehacer la página (ver setTheme): así basta con repintarlo.
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

// Título y subtítulo apilados, recortados los dos: ver *Card widths*.
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

// ---------------------------------------------------------------------------

SettingsView::SettingsView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("settings");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 4, 9, 8);
    col->setSpacing(0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->addStretch();

    // Todo dentro del desplazamiento, como en los cumpleaños: así el alto
    // mínimo de la página es una constante y no depende de cuántos micrófonos
    // tenga el equipo enchufados.
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
    // Los colores de la hoja de estilos bajan solos desde el Panel; aquí solo
    // hay que repintar lo que se dibuja a mano y lee el acento.
    for (QWidget *w : findChildren<QWidget *>()) w->update();
}

void SettingsView::refresh() {
    m_updateSub = nullptr;   // lo que hubiera muere en este mismo barrido
    m_updateBtn = nullptr;
    m_driveSub = nullptr;
    m_driveBtn = nullptr;
    m_group = nullptr;
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();          // ver *Removing rows*: quitarla del layout no la borra
            w->deleteLater();
        }
        delete item;
    }
    if (!m_store) return;

    addAppearance();
    addLanguage();
    addWindow();
    addData();
    addDrive();
    addInputs();
    addUpdates();
    addQuit();
}

// Cada sección es una cabecera (icono en el acento + nombre) y un grupo con
// sus filas separadas por una línea fina. Antes cada ajuste flotaba suelto,
// unas filas con tarjeta y otras sin ella, y con las cabeceras en gris de 9 px
// no se veía dónde acababa una sección y empezaba la siguiente.
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

// Una fila del grupo sin fondo al pasar: las que contienen sus propios mandos
// (puntos, deslizador, segmentos) no son pulsables en sí.
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

    // --- acento ---
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

    // --- opacidad ---
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

    // --- tamaño de texto ---
    // Cuatro tamaños y no un deslizador: entre 88 y 130 no hay nada que afinar,
    // y un botón por tamaño dice de un vistazo cuál está puesto. Cada uno
    // enseña su "Aa" al tamaño que da, así la fila se explica sola en los dos
    // idiomas y no necesita cuatro palabras que no caben (ver *Card widths*).
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
            // Con selector: una regla suelta bajaría a todo lo que cuelgue del botón.
            buttons[i]->setStyleSheet(QString("QToolButton#segOption { font-size: %1px; }")
                                          .arg(11.0 * levels[i].percent / 100.0, 0, 'f', 1));
        }

        // Una muestra con los mismos nombres de objeto que una tarjeta, así que
        // crece con la hoja de estilos igual que las notas de verdad.
        auto *card = new QFrame;
        card->setObjectName("setCard");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(10, 8, 10, 9);
        cl->setSpacing(3);
        auto *title = new QLabel(L("Comprar pan y café"));
        title->setObjectName("cardTitle");
        title->setWordWrap(true);
        title->setMinimumWidth(24);   // ver *Card widths*
        auto *body = new QLabel(L("Así se verán las notas, las listas y el planificador."));
        body->setObjectName("body");
        body->setWordWrap(true);
        body->setMinimumWidth(24);
        cl->addWidget(title);
        cl->addWidget(body);
        b->addWidget(card);
    }
}

void SettingsView::addLanguage() {
    beginGroup(L("IDIOMA"), "globe");
    // Los nombres van cada uno en su propio idioma, no traducidos: quien abre
    // esto con la interfaz en el que no entiende tiene que reconocer el otro.
    addBlock()->addWidget(segments({"Español", "English"},
                                   m_store->prefs().lang == Lang::Es ? 0 : 1,
                                   [this](int i) {
                                       emit languagePicked(i == 0 ? Lang::Es : Lang::En);
                                   }));
}

void SettingsView::addWindow() {
    beginGroup(L("VENTANA"), "window");
    addToggle(L("Siempre encima"),
              m_store->prefs().onTop ? L("Por encima de todo")
                                     : L("Pegada al escritorio"),
              m_store->prefs().onTop, [this](bool on) { emit onTopToggled(on); });
    addToggle(L("Tamaño por página"),
              m_store->prefs().sizePerPage ? L("Cada página recuerda el suyo")
                                           : L("El mismo para todas"),
              m_store->prefs().sizePerPage, [this](bool on) { emit sizePerPageToggled(on); });

#ifdef Q_OS_LINUX
    // Ver choosePlatform() en main.cpp: de esto depende que el panel pueda
    // abrirse hacia el centro de la pantalla y que "siempre encima" se cumpla.
    // Fuera de Linux no hay tal disyuntiva y la fila no pinta nada.
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

void SettingsView::setDrive(const DriveSync *drive) {
    m_drive = drive;
    refresh();
}

void SettingsView::addDrive() {
    if (!m_drive) return;
    beginGroup(L("GOOGLE DRIVE"), "cloud");

    // Recortado, con el texto entero en la ayuda: un error de Google puede ser
    // largo, y aquí no puede ensanchar la página (ver *Card widths*).
    m_driveBtn = addAction(L("Sincronizar con Google Drive"), QString(), QString(), true,
                           [this](QWidget *) {
        switch (m_drive->state()) {
            case DriveSync::Disconnected: emit driveConnectRequested(); break;
            case DriveSync::Authorizing:  emit driveCancelRequested(); break;
            case DriveSync::Idle:
            case DriveSync::Failed:       emit driveSyncRequested(); break;
            default: break;
        }
    }, &m_driveSub);

    m_driveBuiltConnected = m_drive->connected();
    if (m_driveBuiltConnected) {
        // Qué hace y cómo se deja de hacer, dentro del mismo grupo.
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 6, 6, 7);
        rl->setSpacing(8);
        auto *what = new QLabel(L("Tus equipos conectados a esta cuenta comparten notas, tareas, cumpleaños y temporizadores a través de la carpeta Tagoror de tu Drive."));
        what->setObjectName("meta");
        what->setWordWrap(true);
        what->setMinimumWidth(24);   // ver *Card widths*
        rl->addWidget(what, 1);
        auto *off = actionButton(L("Desconectar"));
        connect(off, &QToolButton::clicked, this, [this] { emit driveDisconnectRequested(); });
        rl->addWidget(off, 0, Qt::AlignVCenter);
        addToGroup(row);
    }
    refreshDrive();
}

void SettingsView::refreshDrive() {
    if (!m_drive || !m_driveSub || !m_driveBtn) return;
    if (m_drive->connected() != m_driveBuiltConnected) {
        refresh();
        return;
    }

    const QString error = m_drive->lastError();
    const QString account = m_drive->account().isEmpty() ? L("Conectado") : m_drive->account();
    QString sub, button;
    bool enabled = true, bad = false;
    switch (m_drive->state()) {
        case DriveSync::Unavailable:
            sub = L("No incluida en esta compilación");
            button = L("Conectar");
            enabled = false;
            break;
        case DriveSync::Disconnected:
            bad = !error.isEmpty();
            sub = bad ? error : L("Sin conectar · las notas se quedan en este equipo");
            button = L("Conectar");
            break;
        case DriveSync::Authorizing:
            sub = L("Autoriza el acceso en el navegador…");
            button = L("Cancelar");
            break;
        case DriveSync::Syncing:
            sub = account + " · " + L("sincronizando…");
            button = L("Sincronizar");
            enabled = false;
            break;
        case DriveSync::Failed:
            bad = true;
            sub = error;
            button = L("Reintentar");
            break;
        case DriveSync::Idle: {
            const QDateTime last = m_drive->lastSync();
            sub = account + " · " +
                  (last.isValid()
                       ? L("sincronizado %1").arg(Lang::locale().toString(last, "d MMM · HH:mm"))
                       : L("sin sincronizar todavía"));
            button = L("Sincronizar");
            break;
        }
    }
    m_driveSub->setText(sub);
    m_driveSub->setToolTip(sub);
    m_driveSub->setColor(bad ? QColor("#ff7a6b") : QColor(Theme::muted()));
    m_driveBtn->setText(button);
    m_driveBtn->setEnabled(enabled);
}

void SettingsView::addInputs() {
    beginGroup(L("MICRÓFONO"), "voice");

    const QAudioDevice current = QMediaDevices::defaultAudioInput();
    const QByteArray chosen = m_store->prefs().input;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();

    if (devices.isEmpty()) {
        auto *none = new QLabel(L("No hay micrófono disponible"));
        none->setObjectName("meta");
        none->setWordWrap(true);   // ver *Card widths*: no puede pedir su ancho
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
        // La marca a la derecha, como en los menús del sistema: a la izquierda
        // dejaba las filas sin elegir sangradas sin motivo aparente.
        l->addWidget(new AccentIcon(inUse ? "check" : QString(), &m_theme, 13), 0,
                     Qt::AlignVCenter);
        addToGroup(row);
    }
}

// Qué dice la tarjeta: buscando, lo que falló, hay una nueva, o al día.
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

    // Hay versión nueva: el botón lleva a verla. Si no, busca.
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
    m_updateBtn->setProperty("chosen", hayNueva);   // teñido cuando hay novedad
    if (hayNueva) m_updateSub->setColor(m_theme.accent);
}

// Solo toca los dos trozos que cambian; ver el comentario de la cabecera.
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

// Control segmentado: una pista con las opciones pegadas, que se reparten el
// ancho a partes iguales. Los botones sueltos de antes, cada uno de su ancho,
// quedaban desalineados y no se leían como "uno de estos".
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
    // La ruta se recorta, no ensancha la fila: es la trampa de *Card widths*
    // y una ruta larga es justo lo que la dispara.
    l->addLayout(textColumn(title, subtitle, subOut), 1);

    auto *btn = actionButton(action);
    btn->setEnabled(enabled);
    connect(btn, &QToolButton::clicked, this,
            [clicked, btn] { if (clicked) clicked(btn); });
    l->addWidget(btn, 0, Qt::AlignVCenter);
    addToGroup(row);
    return btn;
}
