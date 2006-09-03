/*
 * KDevelop DUChain viewer
 *
 * Copyright (c) 2006 Hamish Rodda <rodda@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "duchainmodel.h"

#include <klocale.h>

#include "duchainview_part.h"
#include "topducontext.h"
#include "declaration.h"
#include "definition.h"
#include "use.h"

using namespace KTextEditor;

DUChainModel::ProxyObject::ProxyObject(DUChainModelBase* _parent, DUChainModelBase* _object)
  : parent(_parent)
  , object(_object)
{
}

DUChainModel::DUChainModel(DUChainViewPart* parent)
  : QAbstractItemModel(parent)
{
}

DUChainModel::~DUChainModel()
{
}

void DUChainModel::setTopContext(TopDUContext* context)
{
  if (m_chain != context)
    m_chain = context;

  qDeleteAll(m_proxyObjects);
  m_proxyObjects.clear();

  m_objectCache.clear();

  reset();
}

int DUChainModel::columnCount ( const QModelIndex & parent ) const
{
  Q_UNUSED(parent);

  return 1;
}

QModelIndex DUChainModel::index ( int row, int column, const QModelIndex & parent ) const
{
  if (row < 0 || column < 0 || column > 0 || !m_chain)

  if (parent.isValid()) {
    if (row > 0)
      return QModelIndex();

    return createIndex(row, column, m_chain);
  }

  DUChainModelBase* base = static_cast<DUChainModelBase*>(parent.internalPointer());

  QList<DUChainModelBase*>* items = childItems(base);

  if (row >= items->count())
    return QModelIndex();

  return createIndex(row, column, items->at(row));
}

QModelIndex DUChainModel::parent ( const QModelIndex & index ) const
{
  if (!index.isValid())
    return QModelIndex();

  DUChainModelBase* base = static_cast<DUChainModelBase*>(index.internalPointer());

  if (ProxyObject* proxy = dynamic_cast<ProxyObject*>(base))
    return createIndex(proxy->parent->modelRow, 0, proxy->parent);

  if (DUContext* context = dynamic_cast<DUContext*>(base))
    if (context)
      return createParentIndex(context->parentContext());
    else
      return QModelIndex();

  if (Declaration* dec = dynamic_cast<Declaration*>(base))
    return createParentIndex(dec->context());

  if (Definition* def = dynamic_cast<Definition*>(base))
    return createParentIndex(def->declaration());

  if (Use* use = dynamic_cast<Use*>(base))
    return createParentIndex(use->declaration());

  // Shouldn't really hit this
  Q_ASSERT(false);
  return QModelIndex();
}

QVariant DUChainModel::data(const QModelIndex& index, int role ) const
{
  if (index.isValid())
    return QVariant();

  DUChainModelBase* base = static_cast<DUChainModelBase*>(index.internalPointer());
  if (ProxyObject* proxy = dynamic_cast<ProxyObject*>(base)) {
    base = proxy->parent;
  }

  if (DUContext* context = dynamic_cast<DUContext*>(base)) {
    switch (role) {
      case Qt::DisplayRole:
        return context->localScopeIdentifier().toString();
    }

  } else if (Declaration* dec = dynamic_cast<Declaration*>(base)) {
    switch (role) {
      case Qt::DisplayRole:
        return dec->identifier().toString();
    }

  } else if (Definition* def = dynamic_cast<Definition*>(base)) {
    switch (role) {
      case Qt::DisplayRole:
        return i18n("Definition of %1", def->declaration()->identifier().toString());
    }

  } else if (Use* use = dynamic_cast<Use*>(base)) {
    switch (role) {
      case Qt::DisplayRole:
        return i18n("Use of %1", use->declaration()->identifier().toString());
    }
  }

  return QVariant();
}

int DUChainModel::rowCount ( const QModelIndex & parent ) const
{
  if (!parent.isValid())
    return 1;

  DUChainModelBase* base = static_cast<DUChainModelBase*>(parent.internalPointer());
  QList<DUChainModelBase*>* items = childItems(base);
  if (!items)
    return 0;

  return items->count();
}

#define TEST_NEXT(iterator)\
      current = nextItem(iterator, firstInit); \
      if (current.isValid() && current < first) { \
        first = current; \
        currentItem = item(iterator); \
      }

#define TEST_PROXY_NEXT(iterator)\
      current = nextItem(iterator, firstInit); \
      if (current.isValid() && current < first) { \
        first = current; \
        currentItem = proxyItem(parent, iterator); \
      }

QList< DUChainModelBase * >* DUChainModel::childItems(DUChainModelBase * parent) const
{
  if (m_objectCache.contains(parent))
    return m_objectCache[parent];

  QList<DUChainModelBase*>* list = 0;

  if (DUContext* context = dynamic_cast<DUContext*>(parent)) {
    list = new QList<DUChainModelBase*>();

    QListIterator<DUContext*> contexts = context->childContexts();
    QListIterator<DUContext*> importedParentContexts = context->importedParentContexts();
    QListIterator<Declaration*> declarations = context->localDeclarations();
    QListIterator<Definition*> definitions = context->localDefinitions();
    QListIterator<Use*> uses = context->uses();

    bool firstInit = true;
    forever {
      DUChainModelBase* currentItem = 0;
      Cursor first, current;

      currentItem = nextItem(contexts);
      first = current = nextItem(contexts, firstInit);

      TEST_NEXT(importedParentContexts)
      TEST_NEXT(declarations)
      TEST_PROXY_NEXT(definitions)
      TEST_PROXY_NEXT(uses)

      if (currentItem) {
        currentItem->modelRow = list->count();
        list->append(currentItem);

      } else {
        break;
      }

      firstInit = false;
    }

  } else if (Declaration* dec = dynamic_cast<Declaration*>(parent)) {
    if (dec->definition())
      list->append(static_cast<DUChainModelBase*>(dec->definition()));

    foreach (Use* use, dec->uses())
      list->append(static_cast<DUChainModelBase*>(use));

  } else {
    // No child items for definitions or uses
  }

  if (list)
    m_objectCache.insert(parent, list);

  return list;
}

#include "duchainmodel.moc"

// kate: space-indent on; indent-width 2; replace-tabs on
