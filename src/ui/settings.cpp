#include "ui/settings.hpp"

#include "audio/recorder.hpp"
#include "core/lang.hpp"
#include "core/updater.hpp"
#include "ui/elidedlabel.hpp"

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

        if (current || m_hover) {
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
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint())) return;
        m_on = !m_on;
        update();
        if (m_changed) m_changed(m_on);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QColor accent = m_theme ? m_theme->accent : QColor("#7c9cff");
        p.setPen(Qt::NoPen);
        p.setBrush(m_on ? accent : QColor(255, 255, 255, 28));
        p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);

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

// Fila pulsable con fondo propio, como las del calendario y los cumpleaños.
class SettingRow : public QWidget {
public:
    explicit SettingRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("setRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
    }

    std::function<void()> click;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && click)
            click();
    }
};

}  // namespace

// ---------------------------------------------------------------------------

SettingsView::SettingsView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("settings");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
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
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();          // ver *Removing rows*: quitarla del layout no la borra
            w->deleteLater();
        }
        delete item;
    }
    if (!m_store) return;

    addAccent();
    addOpacity();
    addLanguage();
    addWindow();
    addData();
    addInputs();
    addUpdates();
    addQuit();
}

void SettingsView::addSection(const QString &title) {
    auto *label = new QLabel(title);
    label->setObjectName("setSection");
    m_layout->insertWidget(m_layout->count() - 1, label);
}

void SettingsView::addAccent() {
    addSection(L("ACENTO"));

    auto *host = new QWidget;
    auto *l = new QHBoxLayout(host);
    l->setContentsMargins(0, 0, 0, 2);
    l->setSpacing(2);

    const QList<QColor> swatches = {QColor("#7c9cff"), QColor("#6fcf97"), QColor("#f2b757"),
                                    QColor("#ff7a6b"), QColor("#b98cff"), QColor("#4ecdc4")};
    for (const QColor &c : swatches)
        l->addWidget(new ColorDot(c, &m_theme, [this, c] { emit accentPicked(c); }));
    l->addStretch();

    auto *custom = new QToolButton;
    custom->setObjectName("segButton");
    custom->setText(L("Otro…"));
    custom->setCursor(Qt::PointingHandCursor);
    custom->setToolTip(L("Color personalizado…"));
    connect(custom, &QToolButton::clicked, this,
            [this, custom] { emit accentEditorRequested(custom); });
    l->addWidget(custom);

    m_layout->insertWidget(m_layout->count() - 1, host);
}

void SettingsView::addOpacity() {
    auto *head = new QWidget;
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 8, 0, 0);
    hl->setSpacing(6);

    auto *title = new QLabel(L("OPACIDAD"));
    title->setObjectName("setSection");
    auto *readout = new QLabel(QString("%1%").arg(m_theme.opacity));
    readout->setObjectName("setValue");
    readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hl->addWidget(title, 1);
    hl->addWidget(readout);
    m_layout->insertWidget(m_layout->count() - 1, head);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(40, 100);
    slider->setValue(m_theme.opacity);
    connect(slider, &QSlider::valueChanged, this, [this, readout](int v) {
        readout->setText(QString("%1%").arg(v));
        emit opacityChanged(v);
    });
    m_layout->insertWidget(m_layout->count() - 1, slider);
}

void SettingsView::addLanguage() {
    addSection(L("IDIOMA"));
    // Los nombres van cada uno en su propio idioma, no traducidos: quien abre
    // esto con la interfaz en el que no entiende tiene que reconocer el otro.
    addSegments({"Español", "English"}, m_store->prefs().lang == Lang::Es ? 0 : 1,
                [this](int i) { emit languagePicked(i == 0 ? Lang::Es : Lang::En); });
}

void SettingsView::addWindow() {
    addSection(L("VENTANA"));
    addToggle(L("Siempre encima"),
              m_store->prefs().onTop ? L("Por encima de todo")
                                     : L("Pegada al escritorio"),
              m_store->prefs().onTop, [this](bool on) { emit onTopToggled(on); });

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
    addSection(L("DATOS"));
    addCard(L("Carpeta de guardado"),
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

    addCard(L("Copias de seguridad"), sub, L("Gestionar"), m_store->available(),
            [this](QWidget *anchor) { emit backupsRequested(anchor); });
}

void SettingsView::addInputs() {
    addSection(L("MICRÓFONO"));

    const QAudioDevice current = QMediaDevices::defaultAudioInput();
    const QByteArray chosen = m_store->prefs().input;
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();

    if (devices.isEmpty()) {
        auto *none = new QLabel(L("No hay micrófono disponible"));
        none->setObjectName("meta");
        none->setWordWrap(true);   // ver *Card widths*: no puede pedir su ancho
        none->setContentsMargins(7, 4, 7, 4);
        m_layout->insertWidget(m_layout->count() - 1, none);
        return;
    }

    for (const QAudioDevice &dev : devices) {
        const bool inUse = chosen.isEmpty() ? dev.id() == current.id() : dev.id() == chosen;

        auto *row = new SettingRow;
        row->setCursor(Qt::PointingHandCursor);
        row->click = [this, id = dev.id()] { emit inputPicked(id); };

        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(7, 5, 7, 5);
        l->setSpacing(8);

        auto *tick = new QLabel;
        tick->setFixedSize(13, 13);
        if (inUse)
            tick->setPixmap(paintIcon("check", m_theme.accent, 13).pixmap(13, 13));
        l->addWidget(tick);

        auto *name = new ElidedLabel(dev.description(), QColor(inUse ? Theme::fg()
                                                                    : Theme::muted()));
        name->setObjectName("setRowText");
        name->setToolTip(dev.description());
        l->addWidget(name, 1);
        m_layout->insertWidget(m_layout->count() - 1, row);
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
    addSection(L("ACTUALIZACIONES"));

    const bool on = m_store->prefs().updateCheck;
    addToggle(L("Buscar automáticamente"),
              on ? L("Una vez al día") : L("Desactivado · solo a mano"), on,
              [this](bool v) { emit updateCheckToggled(v); });

    // Hay versión nueva: el botón lleva a verla. Si no, busca.
    const QString latest = m_store->prefs().latestSeen;
    const bool hayNueva = !latest.isEmpty() &&
                          Updater::compare(latest, Updater::current()) > 0;

    auto *card = new QFrame;
    card->setObjectName("setCard");
    auto *l = new QHBoxLayout(card);
    l->setContentsMargins(9, 7, 7, 7);
    l->setSpacing(8);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(1);

    auto *name = new ElidedLabel(QString("Tagoror %1").arg(Updater::current()),
                                 QColor(Theme::fg()));
    name->setObjectName("setRowText");
    texts->addWidget(name);

    m_updateSub = new ElidedLabel(updateSubtitle(m_updateBusy, m_updateError),
                                  QColor(hayNueva ? m_theme.accent : QColor(Theme::muted())));
    m_updateSub->setObjectName("meta");
    texts->addWidget(m_updateSub);
    l->addLayout(texts, 1);

    m_updateBtn = new QToolButton;
    m_updateBtn->setObjectName("segButton");
    m_updateBtn->setProperty("chosen", hayNueva);   // teñido cuando hay novedad
    m_updateBtn->setText(hayNueva ? L("Ver") : L("Buscar ahora"));
    m_updateBtn->setEnabled(!m_updateBusy);
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_updateBtn, &QToolButton::clicked, this, [this, hayNueva] {
        if (hayNueva) emit openLatestRequested();
        else emit checkUpdatesRequested();
    });
    l->addWidget(m_updateBtn, 0, Qt::AlignVCenter);

    m_layout->insertWidget(m_layout->count() - 1, card);
}

// Solo toca los dos trozos que cambian; ver el comentario de la cabecera.
void SettingsView::setUpdateState(bool busy, const QString &error) {
    m_updateBusy = busy;
    m_updateError = error;
    if (!m_updateSub || !m_updateBtn) return;

    m_updateSub->setText(updateSubtitle(busy, error));
    m_updateBtn->setEnabled(!busy);
}

void SettingsView::addQuit() {
    auto *host = new QWidget;
    auto *l = new QHBoxLayout(host);
    l->setContentsMargins(0, 12, 0, 0);

    auto *quit = new QToolButton;
    quit->setObjectName("quitBtn");
    quit->setText(L("Salir de Tagoror"));
    quit->setIcon(paintIcon("power", QColor("#ff7a6b"), 13));
    quit->setIconSize(QSize(13, 13));
    quit->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    quit->setCursor(Qt::PointingHandCursor);
    connect(quit, &QToolButton::clicked, this, [this] { emit quitRequested(); });

    l->addWidget(quit);
    l->addStretch();
    m_layout->insertWidget(m_layout->count() - 1, host);
}

void SettingsView::addToggle(const QString &label, const QString &hint, bool on,
                             std::function<void(bool)> changed) {
    auto *row = new SettingRow;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(7, 6, 7, 6);
    l->setSpacing(8);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);

    auto *title = new ElidedLabel(label, QColor(Theme::fg()));
    title->setObjectName("setRowText");
    texts->addWidget(title);
    if (!hint.isEmpty()) {
        auto *sub = new ElidedLabel(hint, QColor(Theme::muted()));
        sub->setObjectName("meta");
        texts->addWidget(sub);
    }
    l->addLayout(texts, 1);
    l->addWidget(new Switch(on, &m_theme, std::move(changed)), 0, Qt::AlignVCenter);
    m_layout->insertWidget(m_layout->count() - 1, row);
}

void SettingsView::addSegments(const QStringList &labels, int chosen,
                               std::function<void(int)> picked) {
    auto *host = new QWidget;
    auto *l = new QHBoxLayout(host);
    l->setContentsMargins(0, 0, 0, 2);
    l->setSpacing(4);

    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QToolButton;
        b->setObjectName("segButton");
        b->setText(labels.at(i));
        b->setProperty("chosen", i == chosen);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [picked, i] { if (picked) picked(i); });
        l->addWidget(b, 1);
    }
    m_layout->insertWidget(m_layout->count() - 1, host);
}

void SettingsView::addCard(const QString &title, const QString &subtitle,
                           const QString &action, bool enabled,
                           std::function<void(QWidget *)> clicked) {
    auto *card = new QFrame;
    card->setObjectName("setCard");

    auto *l = new QHBoxLayout(card);
    l->setContentsMargins(9, 7, 7, 7);
    l->setSpacing(8);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(1);

    auto *name = new ElidedLabel(title, QColor(Theme::fg()));
    name->setObjectName("setRowText");
    texts->addWidget(name);

    // La ruta se recorta, no ensancha la tarjeta: es la trampa de *Card widths*
    // y una ruta larga es justo lo que la dispara.
    auto *sub = new ElidedLabel(subtitle, QColor(Theme::muted()));
    sub->setObjectName("meta");
    sub->setToolTip(subtitle);
    texts->addWidget(sub);
    l->addLayout(texts, 1);

    auto *btn = new QToolButton;
    btn->setObjectName("segButton");
    btn->setText(action);
    btn->setEnabled(enabled);
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QToolButton::clicked, this,
            [clicked, btn] { if (clicked) clicked(btn); });
    l->addWidget(btn, 0, Qt::AlignVCenter);

    m_layout->insertWidget(m_layout->count() - 1, card);
}
