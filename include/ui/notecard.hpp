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
    /// @param theme The whole Theme, not only the accent: cards open their own
    ///              popups, which must respect the configured opacity.
    NoteCard(Note *note, const Theme &theme, QWidget *parent = nullptr);

    Note *note() const { return m_note; }

    /// Repaints the reminder state (ringing / overdue) without rebuilding the
    /// card, so edit focus is kept.
    void refreshDue();

    /// Puts the cursor in the title (a note just created from the planner is
    /// waiting for a name).
    void focusTitle();

signals:
    void dirty();                 ///< Content changed: save.
    void deleteRequested(Note *);
    void dismissRequested(Note *);   ///< Stop this reminder's alarm.
    /// The date of a ringing reminder was changed: the card already cleared
    /// @c ringing, but the tone and the dock belong to the panel.
    void rescheduled(Note *);

    /// One step up (-1) or down (+1) from the menu; dragging the grip is separate
    /// because it has to follow the mouse.
    void moveRequested(Note *, int steps);
    /// "Move to…": the panel knows which areas exist.
    void areaMenuRequested(Note *, const QPoint &globalPos);
    void dragStarted();
    void dragMoved(const QPoint &globalPos);
    void dragFinished();

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    /// Watches the details editor: left empty, it closes itself on focus-out.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void build();
    void buildTitleRow(QVBoxLayout *l);
    void buildCheck(QVBoxLayout *l);
    void buildReminder(QVBoxLayout *l);
    void buildText(QVBoxLayout *l);
    void buildVoice(QVBoxLayout *l);
    void addCheckRow(QVBoxLayout *l, int index);
    void rebuildItems();          ///< Renumbers the rows after a structural change.
    void refreshProgress();
    /// Renames an item. The row being edited draws a field instead of its label,
    /// so it is enough to record which one and rebuild: the indices the lambdas
    /// captured stay correct.
    void beginItemEdit(int index);
    void commitItemEdit(int index, const QString &text);
    void cancelItemEdit();
    /// Item reordering: each row's grip moves it live inside m_itemsLayout, and on
    /// release the row order is written back to items.
    void dragItemTo(QWidget *row, const QPoint &globalPos);
    void endItemDrag();
    void moveItem(int index, int steps);   ///< Alt+Up / Alt+Down on the checkbox.

    void openDuePopup(const QPoint &globalPos);   ///< From the chip or the context menu.
    /// The only place that moves a reminder's date: instant, label and state
    /// change together so they cannot disagree.
    void applyDue(qint64 whenMs, const QString &label);
    void setRepeat(Note::Repeat repeat);
    void refreshRepeat();

    /// A reminder without a body shows only its date; the editor appears when
    /// asked for instead of taking room just in case.
    void showDetailsGhost();
    void showDetailsEditor(bool focus);

    void buildImages(QVBoxLayout *l);
    void refreshImages();
    void addImages();
    void toggleImages();
    void openImageMenu(int index, const QPoint &globalPos);

    void refreshLinks();                         ///< Rebuilds only the rows.
    void openLinkEditor(int index, const QPoint &globalPos);   ///< -1 means a new one.
    void openLinkMenu(int index, const QPoint &globalPos);
    void openLink(int index);

    void ensureAudio();           ///< Builds recorder/player on demand.
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
    int m_editingItem = -1;       ///< Item being renamed, or -1.
    QLineEdit *m_itemEdit = nullptr;   ///< Its field, while it lasts.
    bool m_itemsMoved = false;    ///< The drag in progress already changed the order.

    QWidget *m_linksBox = nullptr;       ///< Container of the link rows.
    QVBoxLayout *m_linksLayout = nullptr;

    QWidget *m_imagesBox = nullptr;      ///< Foldable header plus thumbnail strip.
    QWidget *m_imagesStrip = nullptr;
    QVBoxLayout *m_imagesLayout = nullptr;
    QLabel *m_imagesTitle = nullptr;
    QToolButton *m_imagesToggle = nullptr;

    /// Slot for a reminder's body: holds either the editor or the "add details"
    /// line, and swapping them touches nothing else.
    QWidget *m_detailSlot = nullptr;
    QVBoxLayout *m_detailLayout = nullptr;
    QTextEdit *m_detailBody = nullptr;   ///< Alive only while the editor is shown.
    QLabel *m_repeatChip = nullptr;

    QLabel *m_chip = nullptr;
    QLabel *m_dueIcon = nullptr;
    QToolButton *m_dueBtn = nullptr;     ///< "Stop" while the reminder rings.
    QToolButton *m_recBtn = nullptr;
    QToolButton *m_playBtn = nullptr;
    Waveform *m_wave = nullptr;
    VoiceRecorder *m_recorder = nullptr;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
};
