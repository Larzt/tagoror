#pragma once

#include <QStringList>
#include <functional>

#include "ui/theme.hpp"

class QWidget;

/// The first start's question (1.3.0): which parts of Tagoror to use. One
/// switch per Features::Info, all on, and *Empezar*. Drawn as an Overlay over
/// the shell, like Seguimiento's forms.
///
/// However it closes — Empezar, Escape or a click outside — @p done gets the
/// keys left off: closing it without looking keeps everything, as before 1.3.
namespace Welcome {

void open(const Theme *theme, QWidget *host, const QStringList &off,
          std::function<void(const QStringList &off)> done);

}  // namespace Welcome
