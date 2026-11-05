#pragma once

#include <QDir>
#include <QObject>
#include <QString>
#include <QTextStream>
#include <functional>

class Panel;
class QWidget;

/// A development tool, compiled only with -DTAGOROR_DEV_TOOLS=ON: walks every
/// page and the main dialogs, saves a PNG of each and a report of what does
/// not fit, then quits. Run by scripts/mobile-shots.sh with
/// TAGOROR_FORCE_MOBILE=1 and the offscreen platform, so the phone layout can
/// be checked without a phone or a CI round.
///
/// It seeds sample data first, so it must run over an empty, throwaway data
/// folder (the script points XDG_DATA_HOME and XDG_CONFIG_HOME at one).
///
/// The report lists, per capture, the widgets that end past the window's right
/// edge (outermost first, with their minimum widths: the culprit is usually the
/// deepest one) and the buttons shorter than kMinTouch.
class ScreenshotTour : public QObject {
public:
    static constexpr int kMinTouch = 44;

    ScreenshotTour(Panel *panel, const QString &dir);

private:
    struct Step {
        QString name;
        std::function<void()> open;
        std::function<void()> close;
    };

    void seed();
    void next();
    void capture(const QString &name);
    void report(const QString &name, QWidget *root, QTextStream &out) const;
    void closeEverything();

    Panel *m_panel;
    QDir m_dir;
    QList<Step> m_steps;
    int m_at = 0;
    QString m_report;
};
