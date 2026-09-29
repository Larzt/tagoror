#pragma once

#include <QByteArray>
#include <QColor>
#include <QWidget>

#include "core/drivesync.hpp"
#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/theme.hpp"

class ElidedLabel;
class QFrame;
class QToolButton;
class QVBoxLayout;

/// The settings page. It used to be a Popup and outgrew it: a menu row has no
/// room for a switch, a slider and a folder path.
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

    /// Drive state changes on its own (an upload finishing, a token expiring), so
    /// the card is rewritten in place. Only switching between connected and not
    /// connected rebuilds the page, since what is below changes.
    void setDrive(const DriveSync *drive);
    void refreshDrive();

signals:
    void accentPicked(const QColor &c);
    void accentEditorRequested(QWidget *anchor);
    void opacityChanged(int value);          ///< Live, while dragging.
    void languagePicked(Lang::Code code);
    void textScalePicked(int percent);
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
    void driveConnectRequested();
    void driveCancelRequested();
    void driveSyncRequested();
    void driveDisconnectRequested();
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
    void addLanguage();
    void addWindow();
    void addData();
    void addInputs();
    void addUpdates();
    void addDrive();
    void addCalendar();
    /// What the Calendar section shows; when it changes the page is rebuilt.
    QString calendarSignature() const;
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

    const DriveSync *m_drive = nullptr;
    class ElidedLabel *m_driveSub = nullptr;
    class QToolButton *m_driveBtn = nullptr;
    bool m_driveBuiltConnected = false;
    QString m_calendarBuilt;   ///< calendarSignature() the section was built with.
};
