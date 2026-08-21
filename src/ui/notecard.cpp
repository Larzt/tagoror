#include "ui/notecard.hpp"
#include "ui/popup.hpp"
#include "audio/recorder.hpp"
#include "ui/theme.hpp"
#include "audio/wave.hpp"
#include "ui/waveform.hpp"

#include <QAbstractTextDocumentLayout>
#include <QAudioOutput>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QStyle>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QProgressBar>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Editor de cuerpo que crece con su contenido: sin barras de scroll propias,
// la altura sigue al documento entre un mínimo y un máximo.
QTextEdit *autoGrowEditor(const QString &text, const QString &placeholder) {
    auto *e = new QTextEdit;
    // Sin el menú nativo de cortar/pegar: el clic derecho es para el menú de
    // la nota. Los atajos de teclado siguen funcionando igual.
    e->setContextMenuPolicy(Qt::NoContextMenu);
    e->setPlainText(text);
    e->setPlaceholderText(placeholder);
    e->setAcceptRichText(false);
    e->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    e->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    e->setFixedHeight(46);
    // Fijar la altura no basta: la política de un QTextEdit sigue siendo
    // Expanding, y el layout de la tarjeta la propaga hacia arriba. La lista
    // le daba entonces el hueco sobrante de una ventana alta, y como el editor
    // no puede crecer, se lo quedaba la etiqueta de abajo en forma de hueco.
    e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QObject::connect(e->document()->documentLayout(),
                     &QAbstractTextDocumentLayout::documentSizeChanged, e,
                     [e](const QSizeF &s) {
                         e->setFixedHeight(qBound(46, int(s.height()) + 14, 170));
                     });
    return e;
}

// El chip de fecha es pulsable: cambiar el recordatorio no debería obligar a
// buscar la opción en el menú contextual.
class ClickableLabel : public QLabel {
public:
    ClickableLabel(const QString &text, std::function<void(const QPoint &)> onClick)
        : QLabel(text), m_click(std::move(onClick)) {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_click)
            m_click(e->globalPosition().toPoint());
    }

private:
    std::function<void(const QPoint &)> m_click;
};

// Tachado de un elemento hecho. Va en la etiqueta, no en la casilla, desde
// que el texto dejó de vivir dentro del QCheckBox.
void strikeOut(QLabel *label, bool on) {
    QFont f = label->font();
    f.setStrikeOut(on);
    label->setFont(f);
}

QString formatMs(qint64 ms) {
    const qint64 total = ms / 1000;
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}

QString typeLabel(Note::Type t) {
    switch (t) {
        case Note::Check:    return "Lista";
        case Note::Reminder: return "Recordatorio";
        case Note::Voice:    return "Nota de voz";
        default:             return "Nota";
    }
}

QToolButton *roundButton(const QString &kind, const QColor &color, const QString &tip) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, color));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(28, 28);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------

NoteCard::NoteCard(Note *note, const Theme &theme, QWidget *parent)
    : QFrame(parent), m_note(note), m_theme(theme) {
    setObjectName("card");
    build();
}

void NoteCard::build() {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(11, 9, 11, 10);
    l->setSpacing(7);

    m_title = new QLineEdit(m_note->title);
    m_title->setContextMenuPolicy(Qt::NoContextMenu);
    m_title->setObjectName("cardTitleEdit");
    m_title->setPlaceholderText("Sin título");
    connect(m_title, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_note->title = t;
        emit dirty();
    });
    l->addWidget(m_title);

    switch (m_note->type) {
        case Note::Check:    buildCheck(l);    break;
        case Note::Reminder: buildReminder(l); refreshDue(); break;
        case Note::Voice:    buildVoice(l);    break;
        default:             buildText(l);     break;
    }
}

void NoteCard::buildText(QVBoxLayout *l) {
    QTextEdit *body = autoGrowEditor(m_note->body, "Escribe…");
    connect(body, &QTextEdit::textChanged, this, [this, body] {
        m_note->body = body->toPlainText();
        emit dirty();
    });
    l->addWidget(body);

    m_meta = new QLabel("TEXTO");
    m_meta->setObjectName("meta");
    l->addWidget(m_meta);
}

void NoteCard::buildReminder(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);

    // Icono de reloj/campana junto a la fecha: el estado se ve de un vistazo
    // sin tener que leer la etiqueta.
    m_dueIcon = new QLabel;
    m_dueIcon->setFixedSize(14, 14);
    row->addWidget(m_dueIcon, 0, Qt::AlignVCenter);

    m_chip = new ClickableLabel(m_note->due.isEmpty() ? "Sin fecha" : m_note->due,
                                [this](const QPoint &p) { openDuePopup(p); });
    m_chip->setObjectName("chip");
    m_chip->setToolTip("Clic para cambiar la fecha");
    row->addWidget(m_chip);

    // Solo aparece mientras suena la alarma; es la forma de callarla.
    m_dueBtn = roundButton("stop", QColor("#ff7a6b"), "Detener aviso");
    m_dueBtn->setObjectName("dueBtn");
    m_dueBtn->hide();
    connect(m_dueBtn, &QToolButton::clicked, this, [this] {
        emit dismissRequested(m_note);
    });
    row->addWidget(m_dueBtn);
    row->addStretch();

    m_meta = new QLabel("RECORDATORIO");
    m_meta->setObjectName("meta");
    row->addWidget(m_meta);
    l->addLayout(row);

    QTextEdit *body = autoGrowEditor(m_note->body, "Detalles…");
    connect(body, &QTextEdit::textChanged, this, [this, body] {
        m_note->body = body->toPlainText();
        emit dirty();
    });
    l->addWidget(body);
}

void NoteCard::buildCheck(QVBoxLayout *l) {
    m_itemsLayout = new QVBoxLayout;
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    m_itemsLayout->setSpacing(3);
    l->addLayout(m_itemsLayout);
    rebuildItems();

    // Fila de alta: una casilla punteada (aún no existe) y un campo sin caja,
    // para que se lea como un elemento más de la lista y no como un formulario.
    auto *addRow = new QWidget;
    auto *al = new QHBoxLayout(addRow);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(8);

    auto *ghost = new QLabel;
    ghost->setFixedSize(13, 13);
    ghost->setPixmap(paintIcon("checkdots", QColor(Theme::muted()), 13).pixmap(13, 13));
    al->addWidget(ghost, 0, Qt::AlignVCenter);

    m_newItem = new QLineEdit;
    m_newItem->setContextMenuPolicy(Qt::NoContextMenu);
    m_newItem->setObjectName("newItemEdit");
    m_newItem->setPlaceholderText("Añadir elemento…");
    connect(m_newItem, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_newItem->text().trimmed();
        if (text.isEmpty()) return;
        m_note->items.append(CheckItem{text, false});
        m_newItem->clear();
        rebuildItems();
        refreshProgress();
        emit dirty();
    });
    al->addWidget(m_newItem, 1);
    l->addWidget(addRow);

    auto *foot = new QHBoxLayout;
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);

    m_bar = new QProgressBar;
    m_bar->setTextVisible(false);
    // El acento llega por constructor, así que se aplica en línea: la hoja
    // global no se regenera al cambiar de color sin reconstruir las tarjetas.
    m_bar->setStyleSheet(QString("QProgressBar::chunk { background:%1; border-radius:2px; }")
                             .arg(m_theme.accent.name()));
    foot->addWidget(m_bar, 1);

    m_progress = new QLabel;
    m_progress->setObjectName("meta");
    foot->addWidget(m_progress);
    l->addLayout(foot);

    refreshProgress();
}

void NoteCard::addCheckRow(QVBoxLayout *l, int index) {
    const CheckItem &item = m_note->items.at(index);

    auto *row = new QWidget;
    auto *rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);

    // El texto va en una etiqueta aparte y no dentro del QCheckBox: con el
    // texto dentro, el sizeHint de la casilla es el ancho de la frase entera,
    // ese ancho se convierte en el mínimo de la tarjeta y el QScrollArea
    // ensancha toda la lista por encima del viewport. Como la barra horizontal
    // está desactivada, los elementos largos simplemente desaparecían por la
    // derecha. Con la etiqueta suelta y wordWrap, se parten en varias líneas.
    auto *box = new QCheckBox;
    box->setContextMenuPolicy(Qt::NoContextMenu);
    box->setChecked(item.done);
    box->setCursor(Qt::PointingHandCursor);
    rl->addWidget(box, 0, Qt::AlignTop);

    auto *text = new ClickableLabel(item.text, [box](const QPoint &) { box->toggle(); });
    text->setObjectName("checkText");
    text->setContextMenuPolicy(Qt::NoContextMenu);
    text->setWordWrap(true);
    text->setToolTip(item.text);
    strikeOut(text, item.done);
    rl->addWidget(text, 1);

    connect(box, &QCheckBox::toggled, this, [this, text, index](bool on) {
        if (index >= m_note->items.size()) return;
        m_note->items[index].done = on;
        strikeOut(text, on);
        refreshProgress();
        emit dirty();
    });

    auto *del = new QToolButton;
    del->setIcon(paintIcon("minus", QColor(Theme::muted()), 12));
    del->setIconSize(QSize(12, 12));
    del->setFixedSize(18, 18);
    del->setCursor(Qt::PointingHandCursor);
    del->setToolTip("Quitar elemento");
    connect(del, &QToolButton::clicked, this, [this, index] {
        if (index >= m_note->items.size()) return;
        m_note->items.removeAt(index);
        rebuildItems();
        refreshProgress();
        emit dirty();
    });
    rl->addWidget(del);

    l->addWidget(row);
}

void NoteCard::rebuildItems() {
    // Las filas capturan su índice, así que al borrar una hay que rehacerlas.
    while (QLayoutItem *it = m_itemsLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    for (int i = 0; i < m_note->items.size(); ++i)
        addCheckRow(m_itemsLayout, i);
}

void NoteCard::refreshProgress() {
    if (!m_bar) return;
    const int total = m_note->items.size();
    const int done = m_note->doneCount();

    m_bar->setRange(0, total > 0 ? total : 1);
    m_bar->setValue(done);
    if (m_progress) m_progress->setText(QString("%1/%2").arg(done).arg(total));
}

// --- recordatorio ----------------------------------------------------------

void NoteCard::openDuePopup(const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader("Recordar");

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime todaySix = QDateTime(now.date(), QTime(18, 0));
    if (todaySix <= now) todaySix = todaySix.addDays(1);

    const QList<QPair<QString, QDateTime>> presets = {
        {"En 5 minutos", now.addSecs(300)},
        {"En 1 hora", now.addSecs(3600)},
        {"A las 18:00", todaySix},
        {"Mañana 09:00", QDateTime(now.date().addDays(1), QTime(9, 0))},
    };

    // Los presets guardan el instante real: es lo que dispara el aviso.
    for (const auto &[label, when] : presets) {
        menu->addItem("clock", label, when.toString("ddd d MMM HH:mm"), [this, when] {
            m_note->dueAtMs = when.toMSecsSinceEpoch();
            m_note->due = when.toString("ddd d MMM HH:mm");
            m_note->fired = false;
            if (m_chip) m_chip->setText(m_note->due);
            refreshDue();
            emit dirty();
        });
    }

    menu->addSeparator();
    menu->addHeader("A mano · dd/MM HH:mm");
    menu->addEditor("p. ej. 24/12 20:30", m_note->due, [this](const QString &value) {
        // Si el texto se puede interpretar como fecha, además suena; si no,
        // se queda como etiqueta suelta (comportamiento de siempre).
        QDateTime parsed = QDateTime::fromString(value, "dd/MM HH:mm");
        if (parsed.isValid()) parsed = parsed.addYears(QDate::currentDate().year() - 1900);
        m_note->dueAtMs = parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
        m_note->due = value;
        m_note->fired = false;
        if (m_chip) m_chip->setText(value.isEmpty() ? "Sin fecha" : value);
        refreshDue();
        emit dirty();
    });

    if (!m_note->due.isEmpty()) {
        menu->addSeparator();
        menu->addItem("minus", "Quitar fecha", QString(), [this] {
            m_note->due.clear();
            m_note->dueAtMs = 0;
            m_note->fired = false;
            if (m_chip) m_chip->setText("Sin fecha");
            refreshDue();
            emit dirty();
        });
    }
    menu->showAt(globalPos);
}

void NoteCard::focusTitle() {
    if (!m_title) return;
    m_title->setFocus();
    m_title->selectAll();
}

void NoteCard::refreshDue() {
    if (m_note->type != Note::Reminder) return;

    const bool ringing = m_note->ringing;
    const bool overdue = m_note->dueAtMs > 0 &&
                         QDateTime::currentMSecsSinceEpoch() >= m_note->dueAtMs;

    if (m_dueIcon)
        m_dueIcon->setPixmap(paintIcon(ringing || overdue ? "bell" : "clock",
                                       QColor(ringing || overdue ? "#ff7a6b" : Theme::muted()),
                                       14)
                                 .pixmap(14, 14));
    if (m_dueBtn) m_dueBtn->setVisible(ringing);
    if (m_chip) {
        m_chip->setProperty("state", ringing ? "ringing" : (overdue ? "overdue" : ""));
        // Cambiar una propiedad dinámica no repinta solo: hay que repolish.
        m_chip->style()->unpolish(m_chip);
        m_chip->style()->polish(m_chip);
    }
    if (m_meta)
        m_meta->setText(ringing ? "¡AHORA!" : (overdue ? "VENCIDO" : "RECORDATORIO"));
}

// --- nota de voz -----------------------------------------------------------

void NoteCard::buildVoice(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);

    m_recBtn = roundButton("record", QColor("#ff7a6b"), "Grabar");
    m_recBtn->setObjectName("recBtn");
    connect(m_recBtn, &QToolButton::clicked, this, &NoteCard::toggleRecord);
    row->addWidget(m_recBtn);

    m_playBtn = roundButton("play", QColor(Theme::fg()), "Reproducir");
    m_playBtn->setObjectName("playBtn");
    connect(m_playBtn, &QToolButton::clicked, this, &NoteCard::togglePlay);
    row->addWidget(m_playBtn);

    m_wave = new Waveform;
    m_wave->setAccent(m_theme.accent);
    connect(m_wave, &Waveform::seeked, this, [this](qreal f) {
        if (m_player && m_player->duration() > 0)
            m_player->setPosition(qint64(f * m_player->duration()));
    });
    row->addWidget(m_wave, 1);

    m_progress = new QLabel;
    m_progress->setObjectName("meta");
    row->addWidget(m_progress);
    l->addLayout(row);

    m_meta = new QLabel;
    m_meta->setObjectName("meta");
    l->addWidget(m_meta);

    m_wave->setPeaks(m_note->peaks);
    refreshVoice();

    // Onda ya guardada; si falta (nota de una versión anterior), se calcula
    // leyendo el fichero una sola vez y se persiste. Se aplaza al siguiente
    // ciclo porque dirty() todavía no está conectado durante el constructor
    // —y así construir la lista no se queda leyendo ficheros de disco.
    if (m_note->peaks.isEmpty() && !m_note->audio.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            const WaveScan scan = scanWave(m_note->audioPath());
            if (!scan.ok) return;
            m_note->peaks = scan.peaks;
            m_note->level = int(scan.loudest * 100);
            m_wave->setPeaks(m_note->peaks);
            refreshVoice();
            emit dirty();
        });
    }
}

void NoteCard::ensureAudio() {
    if (m_recorder) return;

    m_recorder = new VoiceRecorder(this);
    connect(m_recorder, &VoiceRecorder::durationChanged, this, [this](qint64 ms) {
        m_note->durationMs = ms;
        if (m_wave) m_wave->setPeaks(m_recorder->peaks());
        refreshVoice();
    });
    connect(m_recorder, &VoiceRecorder::finished, this, [this](bool ok) {
        if (ok) {
            m_note->audio = m_note->id + ".wav";
            m_note->peaks = m_recorder->peaks();
            m_note->level = int(m_recorder->loudest() * 100);
            if (m_wave) {
                m_wave->setLive(false);
                m_wave->setPeaks(m_note->peaks);
            }
            // Regrabar reescribe el mismo fichero: si no se suelta la fuente,
            // el reproductor sigue sirviendo la toma anterior.
            if (m_player) m_player->setSource(QUrl());
            emit dirty();
        }
        refreshVoice();
    });

    m_player = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOut);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        const qint64 total = m_player->duration();
        if (m_wave && total > 0) m_wave->setProgress(qreal(pos) / total);
        if (m_progress) m_progress->setText(formatMs(pos));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState && m_wave) m_wave->setProgress(0);
                refreshVoice();
            });
}

void NoteCard::toggleRecord() {
    ensureAudio();

    if (m_recorder->isRecording()) {
        m_recorder->stop();
        return;
    }

    if (m_player) m_player->stop();
    m_note->durationMs = 0;
    m_note->peaks.clear();
    if (m_wave) {
        m_wave->setLive(true);
        m_wave->setPeaks({});
    }
    // Un fichero por nota: volver a grabar sustituye la toma anterior.
    m_recorder->start(audioDir() + "/" + m_note->id + ".wav");
    refreshVoice();
}

void NoteCard::togglePlay() {
    if (m_note->audio.isEmpty()) return;
    ensureAudio();

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        refreshVoice();
        return;
    }
    if (m_player->source().isEmpty())
        m_player->setSource(QUrl::fromLocalFile(m_note->audioPath()));
    m_player->play();
    refreshVoice();
}

void NoteCard::refreshVoice() {
    const bool recording = m_recorder && m_recorder->isRecording();
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const bool hasAudio = !m_note->audio.isEmpty();

    if (m_recBtn) {
        m_recBtn->setIcon(paintIcon(recording ? "stop" : "record", QColor("#ff7a6b")));
        m_recBtn->setToolTip(recording ? "Detener" : (hasAudio ? "Regrabar" : "Grabar"));
    }
    if (m_playBtn) {
        m_playBtn->setIcon(paintIcon(playing ? "pause" : "play",
                                     QColor(hasAudio && !recording ? Theme::fg() : Theme::muted())));
        m_playBtn->setEnabled(hasAudio && !recording);
    }
    if (m_progress) m_progress->setText(formatMs(m_note->durationMs));

    if (!m_meta) return;
    if (recording) {
        m_meta->setText("GRABANDO…");
    } else if (!hasAudio) {
        const QString err = m_recorder ? m_recorder->errorText() : QString();
        m_meta->setText(err.isEmpty() ? "VOZ · SIN GRABAR" : err.toUpper());
    } else if (m_note->isSilentTake()) {
        // La toma existe pero no tiene señal audible; sin decirlo, el usuario
        // solo ve una nota que "no suena".
        m_meta->setText("VOZ · SIN SEÑAL, REVISA EL MICRÓFONO");
    } else {
        m_meta->setText("VOZ");
    }
}

// ---------------------------------------------------------------------------

void NoteCard::contextMenuEvent(QContextMenuEvent *e) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(typeLabel(m_note->type));

    if (m_note->type == Note::Check && m_newItem) {
        menu->addItem("plus", "Añadir elemento", "Enter para confirmar",
                      [this] { m_newItem->setFocus(); });
        menu->addSeparator();
    }
    if (m_note->type == Note::Reminder) {
        const QPoint at = e->globalPos();
        if (m_note->ringing)
            menu->addItem("stop", "Detener aviso", "Silencia la alarma",
                          [this] { emit dismissRequested(m_note); });
        menu->addItem("clock", "Cambiar fecha",
                      m_note->due.isEmpty() ? "Sin fecha" : m_note->due,
                      [this, at] { openDuePopup(at); });
        menu->addSeparator();
    }
    if (m_note->type == Note::Voice) {
        menu->addItem("record", m_note->audio.isEmpty() ? "Grabar" : "Regrabar",
                      "Sustituye la toma actual", [this] { toggleRecord(); });
        menu->addSeparator();
    }

    menu->addItem("trash", "Eliminar nota", QString(),
                  [this] { emit deleteRequested(m_note); });
    menu->showAt(e->globalPos());
}
