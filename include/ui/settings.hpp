#pragma once

#include <QByteArray>
#include <QColor>
#include <QWidget>

#include "core/synccontroller.hpp"
#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/theme.hpp"

class ElidedLabel;

/// How a typeface preference (Store::Prefs::font) reads to the user.
QString fontDisplayName(const QString &pref);
/// How an alarm tone id (Store::Prefs::alarmTone) reads to the user.
QString alarmToneName(const QString &id);
class QFrame;
class QToolButton;
class QVBoxLayout;

/// The settings page. It used to be a Popup and outgrew it: a menu row has no
/// room for a switch, a slider and a folder path. Since 5.0 it has tabs —
/// General, Apariencia, Sincronización, Atajos, Acerca de — and only the open
/// one is built.
///
/// Like the other pages it touches no disk: it reads the Store and emits one
/// signal per change for Panel to apply and save.
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(const Theme &theme, QWidget *parent = nullptr);

    /// The Store outlives this view; preferences are read on every repaint rather
    /// than copied.
    void setSource(const Store *store);
    /// Does not rebuild the page, unlike the other views: the opacity slider
    /// applies live and each step lands here, so rebuilding would destroy the
    /// slider being dragged. Only what depends on the accent is repainted.
    void setTheme(const Theme &theme);

    void refresh();
    void retranslate() { refresh(); }

    /// Update-check state, written over the existing card instead of rebuilding
    /// the page: the daily check may finish mid-drag of the slider.
    void setUpdateState(bool busy, const QString &error);

    /// Sync state changes on its own (a pass finishing, a token expiring), so
    /// the status line is rewritten in place. Only a change in what the tab
    /// shows (another provider, an account connected) rebuilds it — and never
    /// while a field of it has the focus, or what is being typed would go.
    void setSync(const SyncController *sync);
    void refreshSync();

    enum Tab { General, Appearance, Sync, Shortcuts, About };
    void setTab(Tab tab);

signals:
    void accentPicked(const QColor &c);
    void accentEditorRequested(QWidget *anchor);
    void opacityChanged(int value);          ///< Live, while dragging.
    void languagePicked(Lang::Code code);
    void textScalePicked(int percent);
    void fontEditorRequested(QWidget *anchor);
    void compactPicked(bool compact);
    void alarmToneRequested(QWidget *anchor);   ///< The menu of tones.
    void alarmVolumeChanged(int percent);       ///< Live, while dragging.
    void alarmPreviewRequested();
    void onTopToggled(bool on);
    void appModeToggled(bool on);
    void sizePerPageToggled(bool on);
    void x11Toggled(bool on);
    void dataFolderRequested();
    void backupsRequested(QWidget *anchor);
    void inputPicked(const QByteArray &id);
    void updateCheckToggled(bool on);
    void checkUpdatesRequested();
    void openLatestRequested();
    void quitRequested();
    void googleConnectRequested();
    void googleCancelRequested();
    void googleDisconnectRequested();
    void syncNowRequested();
    void syncProviderPicked(const QString &id);
    void syncWhenPicked(int when);   ///< SyncController::When
    void webdavSaveRequested(const QString &server, const QString &user, const QString &password);
    void webdavForgetRequested();
    void folderChooseRequested();
    void calendarToggled(bool on);
    void calendarGrantRequested();   ///< Re-authorise with the Calendar scope.
    void calendarFollowToggled(const QString &calId, bool on);
    void calendarTargetRequested(QWidget *anchor);

private:
    /// Opens a section: a header with an icon and a group for its rows.
    void beginGroup(const QString &title, const QString &icon);
    /// Adds a row to the open group, with a divider above unless it is the first.
    void addToGroup(QWidget *row);
    /// A vertically stacked group row, for settings with their own controls.
    QVBoxLayout *addBlock();
    void addAppearance();
    void addAlarm();
    void addLanguage();
    void addWindow();
    void addData();
    void addInputs();
    void addUpdates();
    void addSync();
    void addProvider(const QString &id, const QString &icon, const QString &title,
                     const QString &hint, bool chosen);
    void addCalendar();
    void addShortcuts();
    void addAbout();
    /// What the sync tab shows; when it changes the tab is rebuilt.
    QString syncSignature() const;
    /// "Sincronizado hace 2 min", "Sincronizando…", the error.
    QString syncStatus(bool *bad) const;
    void addQuit();
    /// What the update card says right now.
    QString updateSubtitle(bool busy, const QString &error) const;
    /// A row with a switch: label on the left, control on the right.
    void addToggle(const QString &label, const QString &hint, bool on,
                   std::function<void(bool)> changed);
    /// Segmented control: exactly one of its options is chosen.
    QWidget *segments(const QStringList &labels, int chosen, std::function<void(int)> picked,
                      QList<QToolButton *> *out = nullptr);
    /// A row with a text and a button.
    /// @return The button; the subtitle label goes to @p subOut if given.
    QToolButton *addAction(const QString &title, const QString &subtitle,
                                 const QString &action, bool enabled,
                                 std::function<void(QWidget *)> clicked,
                                 ElidedLabel **subOut = nullptr);

    Theme m_theme;
    const Store *m_store = nullptr;
    QVBoxLayout *m_layout = nullptr;   ///< Content plus the trailing stretch.
    class QFrame *m_group = nullptr;   ///< Group of the section being built.
    QVBoxLayout *m_groupLayout = nullptr;

    /// Pieces of the update card, rewritten without rebuilding.
    class ElidedLabel *m_updateSub = nullptr;
    class QToolButton *m_updateBtn = nullptr;
    bool m_updateBusy = false;
    QString m_updateError;

    const SyncController *m_sync = nullptr;
    class ElidedLabel *m_syncStatus = nullptr;
    class QToolButton *m_syncBtn = nullptr;
    QString m_syncBuilt;      ///< syncSignature() the tab was built with.
    Tab m_tab = General;
    QList<class QToolButton *> m_tabButtons;
};
