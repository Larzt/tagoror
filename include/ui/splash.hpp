#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QLabel;
class QVariantAnimation;

/// The start screen (5.0): the twelve stones of the tagoror light up in a
/// ring while a line says what is really happening — loading, syncing,
/// looking for updates. It is shown only when there is something to wait for;
/// a start where everything is local and instant goes straight to the notes,
/// and a start into the tray (`--tray`) shows nothing at all.
///
/// Drawn inside the shell, over it, like the command palette.
class Splash : public QWidget {
public:
    /// One thing being waited for. It moves on when @c done says so or after
    /// @c timeoutMs, whichever comes first: the splash never holds the start
    /// hostage to a slow network, the work just carries on behind the panel.
    struct Step {
        QString text;
        std::function<bool()> done;
        int timeoutMs = 3000;
    };

    Splash(const Theme *theme, QWidget *host);

    /// Shows each step in turn, then fades out and deletes itself.
    void run(const QList<Step> &steps);

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setStep(int index);
    void finish();
    void poll();

    const Theme *m_theme;
    QList<Step> m_steps;
    int m_current = -1;
    QElapsedTimer m_clock;
    QString m_step;
    QString m_count;
    QVariantAnimation *m_spin = nullptr;
    qreal m_fade = 1.0;
    bool m_finishing = false;
};
