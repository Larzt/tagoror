#pragma once

#include <functional>

#include "core/collection.hpp"
#include "ui/theme.hpp"

class QWidget;
class Store;

/// Colecciones' forms (1.3.0), drawn inside the panel as Overlays, like
/// Seguimiento's: the item's card and the collection's. They change the Store
/// themselves and report it; Panel saves and repaints.
namespace colldlg {

struct ItemHooks {
    std::function<void()> saved;                       ///< Created, edited or deleted.
    /// Open (creating it first) the item's Markdown note. Null: the section is
    /// not shown (Markdown notes are turned off).
    std::function<void(CollectionItem *)> openNote;
    /// The linked note's title, if it still exists.
    std::function<QString(const QString &noteId)> noteTitle;
};

/// The item's card. Null @p item: a new one in @p c, in its first state.
void editItem(const Theme *theme, QWidget *host, Store *store, Collection *c,
              CollectionItem *item, const ItemHooks &hooks);

/// A new collection (null @p c), from a template, or an existing one's name,
/// states, fields and yearly goal. @p saved gets it; @p deleted runs after it
/// was deleted with its items.
void editCollection(const Theme *theme, QWidget *host, Store *store, Collection *c,
                    std::function<void(Collection *)> saved, std::function<void()> deleted);

}  // namespace colldlg
