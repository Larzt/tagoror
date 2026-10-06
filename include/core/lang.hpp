#pragma once

#include <QLocale>
#include <QString>

/// Interface language (Spanish or English).
///
/// Literals are written in Spanish in the source and L() swaps them for the
/// English entry when that language is chosen; a key missing from the table
/// falls back to Spanish instead of coming out empty. QTranslator is not used:
/// it would need a lupdate/lrelease step and .qm files next to the binary.
namespace Lang {

enum Code { Es, En };

Code current();
void setCurrent(Code c);

/// Language for a fresh install: Spanish if the system speaks it, English
/// otherwise. Existing data files never go through this (see Store::load).
Code systemDefault();

Code fromString(const QString &s, Code fallback);
QString toString(Code c);

/// Locale used to format dates. Never use QLocale::system(): the language is
/// the setting, not the environment.
QLocale locale();

/// Initial of each calendar column, Monday to Sunday (the week starts on
/// Monday in both languages).
QString weekdayInitial(int column);

}  // namespace Lang

/// Translates an interface text written in Spanish.
QString L(const QString &es);
