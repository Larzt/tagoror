#pragma once

#include <QFrame>
#include "core/note.hpp"
#include "ui/theme.hpp"

class QLabel;
class QProgressBar;
class QVBoxLayout;
class QLineEdit;
class QTextEdit;
class QToolButton;
class QAudioOutput;
class QMediaPlayer;
class VoiceRecorder;
class Waveform;

class NoteCard : public QFrame {
    Q_OBJECT

public:
    // Recibe el Theme completo (no solo el acento) porque las tarjetas abren
    // sus propios popups y deben respetar la opacidad configurada.
    NoteCard(Note *note, const Theme &theme, QWidget *parent = nullptr);

    Note *note() const { return m_note; }

    // Vuelve a pintar el estado del recordatorio (sonando / vencido) sin
    // reconstruir la tarjeta, para no perder el foco de edición.
    void refreshDue();

    // Deja el cursor en el título: es como llega el usuario desde el
    // calendario, con la nota recién creada esperando nombre.
    void focusTitle();

signals:
    void dirty();                 // el contenido cambió → guardar
    void deleteRequested(Note *);
    void dismissRequested(Note *);   // parar la alarma de este recordatorio

    // --- reordenar ---
    // Un paso arriba (-1) o abajo (+1) desde el menú; el arrastre por el
    // asidero va aparte, porque necesita seguir al ratón mientras dura.
    void moveRequested(Note *, int steps);
    void dragStarted();
    void dragMoved(const QPoint &globalPos);
    void dragFinished();

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    // Vigila el editor de detalles: vacío, se cierra solo al salir de él.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void build();
    void buildTitleRow(QVBoxLayout *l);
    void buildCheck(QVBoxLayout *l);
    void buildReminder(QVBoxLayout *l);
    void buildText(QVBoxLayout *l);
    void buildVoice(QVBoxLayout *l);
    void addCheckRow(QVBoxLayout *l, int index);
    void rebuildItems();          // re-numera las filas tras borrar una
    void refreshProgress();

    void openDuePopup(const QPoint &globalPos);   // chip o menú contextual
    void setRepeat(Note::Repeat repeat);
    void refreshRepeat();

    // --- detalles del recordatorio ---
    // Un recordatorio sin cuerpo enseña solo su fecha; el editor aparece
    // cuando se pide, no ocupando sitio por si acaso.
    void showDetailsGhost();
    void showDetailsEditor(bool focus);

    // --- imágenes adjuntas ---
    void buildImages(QVBoxLayout *l);
    void refreshImages();
    void addImages();
    void toggleImages();
    void openImageMenu(int index, const QPoint &globalPos);

    // --- enlaces adjuntos ---
    void refreshLinks();                         // reconstruye solo las filas
    void openLinkEditor(int index, const QPoint &globalPos);   // -1 = uno nuevo
    void openLinkMenu(int index, const QPoint &globalPos);
    void openLink(int index);

    // --- voz ---
    void ensureAudio();           // crea grabador/reproductor bajo demanda
    void toggleRecord();
    void togglePlay();
    void refreshVoice();

    Note *m_note;
    Theme m_theme;
    QLineEdit *m_title = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_progress = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QLabel *m_meta = nullptr;
    QVBoxLayout *m_itemsLayout = nullptr;
    QLineEdit *m_newItem = nullptr;

    QWidget *m_linksBox = nullptr;       // contenedor de las filas de enlaces
    QVBoxLayout *m_linksLayout = nullptr;

    QWidget *m_imagesBox = nullptr;      // cabecera plegable + tira de miniaturas
    QWidget *m_imagesStrip = nullptr;
    QVBoxLayout *m_imagesLayout = nullptr;
    QLabel *m_imagesTitle = nullptr;
    QToolButton *m_imagesToggle = nullptr;

    // Hueco donde vive el cuerpo de un recordatorio: dentro está el editor o
    // el "añadir detalles", y cambiar de uno a otro no toca el resto.
    QWidget *m_detailSlot = nullptr;
    QVBoxLayout *m_detailLayout = nullptr;
    QTextEdit *m_detailBody = nullptr;   // vivo solo mientras hay editor
    QLabel *m_repeatChip = nullptr;

    QLabel *m_chip = nullptr;
    QLabel *m_dueIcon = nullptr;
    QToolButton *m_dueBtn = nullptr;     // "detener" cuando el aviso suena
    QToolButton *m_recBtn = nullptr;
    QToolButton *m_playBtn = nullptr;
    Waveform *m_wave = nullptr;
    VoiceRecorder *m_recorder = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
};
