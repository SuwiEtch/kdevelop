/* This  is part of KDevelop
    Copyright 2006 Hamish Rodda <rodda@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "ducontext.h"
#include "ducontext_p.h"

#include <QMutableLinkedListIterator>
#include <QSet>

#include <ktexteditor/document.h>
#include <ktexteditor/smartinterface.h>

#include "declaration.h"
#include "duchain.h"
#include "duchainlock.h"
#include "use.h"
#include "identifier.h"
#include "typesystem.h"
#include "topducontext.h"
#include "symboltable.h"
#include "aliasdeclaration.h"
#include "namespacealiasdeclaration.h"
#include "abstractfunctiondeclaration.h"
#include <indexedstring.h>
#include <editor/editorintegrator.h>
#include <limits>
#include "duchainregister.h"


///It is fine to use one global static mutex here

using namespace KTextEditor;

template<class T, int num>
QList<T> arrayToList(const QVarLengthArray<T, num>& array) {
  QList<T> ret;
  FOREACH_ARRAY(const T& item, array)
    ret << item;

  return ret;
}

template<class Container, class Type>
bool removeOne(Container& container, const Type& value) {
  int num = 0;
  for(typename Container::const_iterator it = container.begin(); it != container.end(); ++it) {
    if((*it) == value) {
      container.remove(num);
      return true;
    }
    ++num;
  }
  return false;
}

//Stored statically for performance-reasons

#ifndef NDEBUG
#define ENSURE_CAN_WRITE_(x) {if(x->inDUChain()) { ENSURE_CHAIN_WRITE_LOCKED }}
#define ENSURE_CAN_READ_(x) {if(x->inDUChain()) { ENSURE_CHAIN_READ_LOCKED }}
#else
#define ENSURE_CAN_WRITE_(x)
#define ENSURE_CAN_READ_(x)
#endif

namespace KDevelop
{
QMutex DUContextData::m_localDeclarationsMutex(QMutex::Recursive);

REGISTER_DUCHAIN_ITEM(DUContext);

const Identifier globalImportIdentifier("{...import...}");

DUContextData::DUContextData(DUContext* d)
  : m_owner(0), m_context(d), m_anonymousInParent(false), m_propagateDeclarations(false), m_rangesChanged(true)
{
}

void DUContextData::scopeIdentifier(bool includeClasses, QualifiedIdentifier& target) const {
  if (m_parentContext)
    m_parentContext->d_func()->scopeIdentifier(includeClasses, target);

  if (includeClasses || m_contextType != DUContext::Class)
    target += m_scopeIdentifier;
}

bool DUContextData::isThisImportedBy(const DUContext* context) const {
  if( this == context->d_func() )
    return true;

  foreach( const DUContext* ctx, m_importedChildContexts ) {
    if( ctx->d_func()->isThisImportedBy(context) )
      return true;
  }

  return false;
}

void DUContext::synchronizeUsesFromSmart() const
{
  DUCHAIN_D(DUContext);

  if(d->m_rangesForUses.isEmpty() || !d->m_rangesChanged)
    return;

  Q_ASSERT(d->m_rangesForUses.count() == d->m_uses.count());

  for(int a = 0; a < d->m_uses.count(); a++)
    if(d->m_rangesForUses[a])
      d->m_uses[a].m_range = SimpleRange(*d->m_rangesForUses[a]);

  d->m_rangesChanged = false;
}

void DUContext::synchronizeUsesToSmart() const
{
  DUCHAIN_D(DUContext);
  if(d->m_rangesForUses.isEmpty())
    return;
  Q_ASSERT(d->m_rangesForUses.count() == d->m_uses.count());

  // TODO: file close race? from here
  KTextEditor::SmartInterface *iface = qobject_cast<KTextEditor::SmartInterface*>( smartRange()->document() );
  Q_ASSERT(iface);

  // TODO: file close race to here
  QMutexLocker l(iface->smartMutex());

  for(int a = 0; a < d->m_uses.count(); a++) {
    if(a % 10 == 0) { //Unlock the smart-lock time by time, to increase responsiveness
      l.unlock();
      l.relock();
    }
    if(d->m_rangesForUses[a]) {
      d->m_rangesForUses[a]->start() = d->m_uses[a].m_range.start.textCursor();
      d->m_rangesForUses[a]->end() = d->m_uses[a].m_range.end.textCursor();
    }else{
      kDebug() << "bad smart-range";
    }
  }
}

void DUContext::rangePositionChanged(KTextEditor::SmartRange* range)
{
  if(range != smartRange())
    d_func()->m_rangesChanged = true;
}

void DUContext::rangeDeleted(KTextEditor::SmartRange* range)
{
  if(range == smartRange()) {
    DocumentRangeObject::rangeDeleted(range);
  } else {
    range->removeWatcher(this);
    int index = d_func()->m_rangesForUses.indexOf(range);
    if(index != -1) {
      d_func()->m_uses[index].m_range = SimpleRange(*range);
      d_func()->m_rangesForUses[index] = 0;
    }

    if(d_func()->m_rangesForUses.count(0) == d_func()->m_rangesForUses.size())
      d_func()->m_rangesForUses.clear();
  }
}

void DUContextData::addDeclarationToHash(const Identifier& identifier, Declaration* declaration)
{
  m_localDeclarationsHash.insert( identifier, DeclarationPointer(declaration) );

  if( m_propagateDeclarations && m_parentContext )
    m_parentContext->d_func_dynamic()->addDeclarationToHash(identifier, declaration);
}

void DUContextData::removeDeclarationFromHash(const Identifier& identifier, Declaration* declaration)
{
  m_localDeclarationsHash.remove( identifier, DeclarationPointer(declaration) );

  if( m_propagateDeclarations && m_parentContext )
    m_parentContext->d_func_dynamic()->removeDeclarationFromHash(identifier,  declaration);
}

void DUContextData::addDeclaration( Declaration * newDeclaration )
{
  // The definition may not have its identifier set when it's assigned... allow dupes here, TODO catch the error elsewhere
  {
    QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);

//     m_localDeclarations.append(newDeclaration);

  SimpleCursor start = newDeclaration->range().start;

  bool inserted = false;
  for (int i = m_localDeclarations.count()-1; i >= 0; --i) {
    Declaration* child = m_localDeclarations.at(i);
    if(child == newDeclaration)
      return;
    if (start > child->range().start) {
      m_localDeclarations.insert(i+1, newDeclaration);
      inserted = true;
      break;
    }
  }
  if( !inserted )
    m_localDeclarations.append(newDeclaration);

    addDeclarationToHash(newDeclaration->identifier(), newDeclaration);
  }

  //DUChain::contextChanged(m_context, DUChainObserver::Addition, DUChainObserver::LocalDeclarations, newDeclaration);
}

bool DUContextData::removeDeclaration(Declaration* declaration)
{
  QMutexLocker lock(&m_localDeclarationsMutex);

  removeDeclarationFromHash(declaration->identifier(), declaration);

  if( removeOne(m_localDeclarations, declaration) ) {
    //DUChain::contextChanged(m_context, DUChainObserver::Removal, DUChainObserver::LocalDeclarations, declaration);
    return true;
  }else {
    return false;
  }
}

void DUContext::changingIdentifier( Declaration* decl, const Identifier& from, const Identifier& to ) {
  QMutexLocker lock(&d_func()->m_localDeclarationsMutex);
  d_func_dynamic()->removeDeclarationFromHash(from, decl);
  d_func_dynamic()->addDeclarationToHash(to, decl);
}

void DUContextData::addChildContext( DUContext * context )
{
  // Internal, don't need to assert a lock
  context->clearDeclarationIndices();

  Q_ASSERT(!context->d_func()->m_parentContext || context->d_func()->m_parentContext.data()->d_func() == this );

  bool inserted = false;

  int childCount = m_childContexts.count();

  //Optimization: In most cases while parsing, the new child-context will be added to the end, so check if it is the case.
  if(!m_childContexts.isEmpty()) {
    if(m_childContexts.at(childCount-1)->range().start <= context->range().start)
      goto insertAtEnd;
  }

  for (int i = 0; i < childCount; ++i) {
    DUContext* child = m_childContexts.at(i);
    if (context == child)
      return;
    if (context->range().start < child->range().start) {
      m_childContexts.insert(i, context);
      context->d_func_dynamic()->m_parentContext = m_context;
      inserted = true;
      break;
    }
  }

  insertAtEnd:
  if( !inserted ) {
    m_childContexts.append(context);
    context->d_func_dynamic()->m_parentContext = m_context;
  }

  //DUChain::contextChanged(m_context, DUChainObserver::Addition, DUChainObserver::ChildContexts, context);
  context->updateDeclarationIndices();
}

void DUContext::clearDeclarationIndices()
{
  ENSURE_CAN_WRITE

  foreach(Declaration* decl, localDeclarations())
    decl->clearOwnIndex();
  foreach(DUContext* child, childContexts())
    child->clearDeclarationIndices();
}

void DUContext::updateDeclarationIndices()
{
  ENSURE_CAN_WRITE

  foreach(Declaration* decl, localDeclarations())
    decl->allocateOwnIndex();
  foreach(DUContext* child, childContexts())
    child->clearDeclarationIndices();
}

bool DUContextData::removeChildContext( DUContext* context ) {
//   ENSURE_CAN_WRITE

  if( removeOne(m_childContexts, context) )
    return true;
  else
    return false;
}

void DUContextData::addImportedChildContext( DUContext * context )
{
//   ENSURE_CAN_WRITE
  Q_ASSERT(!m_importedChildContexts.contains(context));

  m_importedChildContexts.append(context);

  //DUChain::contextChanged(m_context, DUChainObserver::Addition, DUChainObserver::ImportedChildContexts, context);
}

//Can also be called with a context that is not in the list
void DUContextData::removeImportedChildContext( DUContext * context )
{
//   ENSURE_CAN_WRITE
  removeOne(m_importedChildContexts, context);
  //if( != 0 )
    //DUChain::contextChanged(m_context, DUChainObserver::Removal, DUChainObserver::ImportedChildContexts, context);
}

int DUContext::depth() const
{
  { if (!parentContext()) return 0; return parentContext()->depth() + 1; }
}

DUContext::DUContext(DUContextData& data) : DUChainBase(data) {
  ///@todo care about symbol table when loading
}


DUContext::DUContext(const SimpleRange& range, DUContext* parent, bool anonymous)
  : DUChainBase(*new DUContextData(this), range)
{
  DUCHAIN_D_DYNAMIC(DUContext);
  d->m_contextType = Other;
  d->m_parentContext = 0;
  d->m_anonymousInParent = anonymous;
  d->m_inSymbolTable = false;

  if (parent) {
    if( !anonymous )
      parent->d_func_dynamic()->addChildContext(this);
    else
      d_func_dynamic()->m_parentContext = parent;
  }

  if(parent && !anonymous && parent->inSymbolTable())
    setInSymbolTable(true);
}

DUContext::DUContext( DUContextData& dd, const SimpleRange& range, DUContext * parent, bool anonymous )
  : DUChainBase(dd, range)
{
  DUCHAIN_D_DYNAMIC(DUContext);
  d->m_contextType = Other;
  d->m_parentContext = 0;
  d->m_inSymbolTable = false;
  d->m_anonymousInParent = anonymous;
  if (parent) {
    if( !anonymous )
      parent->d_func_dynamic()->addChildContext(this);
    else
      d_func_dynamic()->m_parentContext = parent;
  }
}

DUContext::DUContext(DUContext& useDataFrom)
  : DUChainBase(useDataFrom)
{
}

DUContext::~DUContext( )
{
  DUCHAIN_D_DYNAMIC(DUContext);
  if(d->m_owner)
    d->m_owner->setInternalContext(0);

  setInSymbolTable(false);

  while( !d->m_importedChildContexts.isEmpty() )
    importedChildContexts().first()->removeImportedParentContext(this);

  while( !d->m_importedContexts.isEmpty() )
    if( d->m_importedContexts.front().context )
      removeImportedParentContext(d->m_importedContexts.front().context.data());
    else
      d->m_importedContexts.pop_front();

  deleteChildContextsRecursively();

  deleteUses();

  deleteLocalDeclarations();

  if (d->m_parentContext)
    d->m_parentContext->d_func_dynamic()->removeChildContext(this);
  //DUChain::contextChanged(this, DUChainObserver::Deletion, DUChainObserver::NotApplicable);
}

const QVector< DUContext * > & DUContext::childContexts( ) const
{
  ENSURE_CAN_READ

  return d_func()->m_childContexts;
}

Declaration* DUContext::owner() const {
  ENSURE_CAN_READ
  return d_func()->m_owner;
}

void DUContext::setOwner(Declaration* owner) {
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);
  if( owner == d->m_owner )
    return;

  Declaration* oldOwner = d->m_owner;

  d->m_owner = owner;

  //Q_ASSERT(!oldOwner || oldOwner->internalContext() == this);
  if( oldOwner && oldOwner->internalContext() == this )
    oldOwner->setInternalContext(0);


  //The context set as internal context should always be the last opened context
  if( owner )
    owner->setInternalContext(this);
}

DUContext* DUContext::parentContext( ) const
{
  //ENSURE_CAN_READ Commented out for performance reasons

  return d_func()->m_parentContext.data();
}

void DUContext::setPropagateDeclarations(bool propagate)
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);
  QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);

  bool oldPropagate = d->m_propagateDeclarations;

  if( oldPropagate && !propagate && d->m_parentContext )
    foreach(const DeclarationPointer& decl, d->m_localDeclarationsHash)
      if(decl)
        d->m_parentContext->d_func_dynamic()->removeDeclarationFromHash(decl->identifier(), decl.data());

  d->m_propagateDeclarations = propagate;

  if( !oldPropagate && propagate && d->m_parentContext ) {
    DUContextData::DeclarationsHash::const_iterator it = d->m_localDeclarationsHash.begin();
    DUContextData::DeclarationsHash::const_iterator end = d->m_localDeclarationsHash.end();
    for( ; it != end ; it++ )
      if(it.value())
        d->m_parentContext->d_func_dynamic()->addDeclarationToHash(it.value()->identifier(), it.value().data());
  }
}

bool DUContext::isPropagateDeclarations() const
{
  return d_func()->m_propagateDeclarations;
}

QList<Declaration*> DUContext::findLocalDeclarations( const Identifier& identifier, const SimpleCursor & position, const TopDUContext* topContext, const AbstractType::Ptr& dataType, SearchFlags flags ) const
{
  ENSURE_CAN_READ

  DeclarationList ret;
  findLocalDeclarationsInternal(identifier, position.isValid() ? position : range().end, dataType, ret, topContext ? topContext : this->topContext(), flags);
  return arrayToList(ret);
}

void DUContext::findLocalDeclarationsInternal( const Identifier& identifier, const SimpleCursor & position, const AbstractType::Ptr& dataType, DeclarationList& ret, const TopDUContext* /*source*/, SearchFlags flags ) const
{
  DUCHAIN_D(DUContext);

  {
     QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);

    QHash<Identifier, DeclarationPointer>::const_iterator it = d->m_localDeclarationsHash.find(identifier);
    QHash<Identifier, DeclarationPointer>::const_iterator end = d->m_localDeclarationsHash.end();

    for( ; it != end && it.key() == identifier; ++it ) {
      Declaration* declaration = (*it).data();

      if( !declaration ) {
        //This should never happen, but let's see
        kDebug(9505) << "DUContext::findLocalDeclarationsInternal: Invalid declaration in local-declaration-hash";
        continue;
      }

      if( declaration->kind() == Declaration::Alias ) {
        //Apply alias declarations
        AliasDeclaration* alias = static_cast<AliasDeclaration*>(declaration);
        if(alias->aliasedDeclaration().isValid()) {
          declaration = alias->aliasedDeclaration().declaration();
        } else {
          kDebug() << "lost aliased declaration";
        }
      }

      if( declaration->kind() == Declaration::NamespaceAlias )
        continue; //Do not include NamespaceAliasDeclarations here, they are handled by DUContext directly.

      if((flags & OnlyFunctions) && !declaration->isFunctionDeclaration())
        continue;

      if (!dataType || dataType == declaration->abstractType())
        if (type() == Class || type() == Template || position > declaration->range().start || !position.isValid()) ///@todo This is C++-specific
          ret.append(declaration);
    }
  }
}

bool DUContext::foundEnough( const DeclarationList& ret ) const {
  if( !ret.isEmpty() )
    return true;
  else
    return false;
}

bool DUContext::findDeclarationsInternal( const SearchItem::PtrList & baseIdentifiers, const SimpleCursor & position, const AbstractType::Ptr& dataType, DeclarationList& ret, const TopDUContext* source, SearchFlags flags ) const
{
  DUCHAIN_D(DUContext);
  if( d_func()->m_contextType != Namespace ) { //If we're in a namespace, delay all the searching into the top-context, because only that has the overview to pick the correct declarations.
    for(int a = 0; a < baseIdentifiers.size(); ++a)
      if(!baseIdentifiers[a]->isExplicitlyGlobal && baseIdentifiers[a]->next.isEmpty()) //It makes no sense searching locally for qualified identifiers
        findLocalDeclarationsInternal(baseIdentifiers[a]->identifier, position, dataType, ret, source, flags);

    if( foundEnough(ret) )
      return true;
  }

  ///Step 1: Apply namespace-aliases and -imports
  SearchItem::PtrList aliasedIdentifiers;
  //Because of namespace-imports and aliases, this identifier may need to be searched under multiple names
  if( d_func()->m_contextType == Namespace )
    applyAliases(baseIdentifiers, aliasedIdentifiers, position, false);
  else
    aliasedIdentifiers = baseIdentifiers;


  if( !d->m_importedContexts.isEmpty() ) {
    ///Step 2: Give identifiers that are not marked as explicitly-global to imported contexts(explicitly global ones are treatead in TopDUContext)
    SearchItem::PtrList nonGlobalIdentifiers;
    FOREACH_ARRAY( const SearchItem::Ptr& identifier, aliasedIdentifiers )
      if( !identifier->isExplicitlyGlobal )
        nonGlobalIdentifiers << identifier;

    if( !nonGlobalIdentifiers.isEmpty() ) {
      QVector<Import>::const_iterator it = d->m_importedContexts.end();
      QVector<Import>::const_iterator begin = d->m_importedContexts.begin();
      while( it != begin ) {
        --it;
        DUContext* context = (*it).context.data();

        while( !context && it != begin ) {
          --it;
          context = (*it).context.data();
        }

        if( !context )
          break;

        if( position.isValid() && (*it).position.isValid() && position < (*it).position )
          continue;

        if( !context->findDeclarationsInternal(nonGlobalIdentifiers,  url() == context->url() ? position : context->range().end, dataType, ret, source, flags | InImportedParentContext) )
          return false;
      }
    }
  }

  if( foundEnough(ret) )
    return true;

  ///Step 3: Continue search in parent-context
  if (!(flags & DontSearchInParent) && shouldSearchInParent(flags) && d_func()->m_parentContext) {
    applyUpwardsAliases(aliasedIdentifiers);
    return d_func()->m_parentContext->findDeclarationsInternal(aliasedIdentifiers, url() == d_func()->m_parentContext->url() ? position : d_func()->m_parentContext->range().end, dataType, ret, source, flags);
  }
  return true;
}

QList<Declaration*> DUContext::findDeclarations( const QualifiedIdentifier & identifier, const SimpleCursor & position, const AbstractType::Ptr& dataType, const TopDUContext* topContext, SearchFlags flags) const
{
  ENSURE_CAN_READ

  DeclarationList ret;
  SearchItem::PtrList identifiers;
  identifiers << SearchItem::Ptr(new SearchItem(identifier));

  findDeclarationsInternal(identifiers, position.isValid() ? position : range().end, dataType, ret, topContext ? topContext : this->topContext(), flags);

  return arrayToList(ret);
}

bool DUContext::imports(const DUContext* origin, const SimpleCursor& /*position*/ ) const
{
  ENSURE_CAN_READ

  return origin->d_func()->isThisImportedBy(this);
}


void DUContext::addImportedParentContext( DUContext * context, const SimpleCursor& position, bool anonymous, bool /*temporary*/ )
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);

  if(context == this) {
    kDebug(9007) << "Tried to import self";
    return;
  }

  for(int a = 0; a < d->m_importedContexts.size(); ++a) {
    if(d->m_importedContexts[a].context.data() == context) {
      d->m_importedContexts[a].position = position;
      return;
    }
  }

  if( !anonymous ) {
    ENSURE_CAN_WRITE_(context)
    context->d_func_dynamic()->addImportedChildContext(this);
  }

  ///Do not sort the imported contexts by their own line-number, it makes no sense.
  ///Contexts added first, aka template-contexts, should stay in first place, so they are searched first.

  d->m_importedContexts.append(Import(context, position));

  //DUChain::contextChanged(this, DUChainObserver::Addition, DUChainObserver::ImportedParentContexts, context);
}

void DUContext::removeImportedParentContext( DUContext * context )
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);

  for(int a = 0; a < d->m_importedContexts.size(); ++a) {
    if(d->m_importedContexts[a].context.data() == context) {
      d->m_importedContexts.remove(a);
      break;
    }
  }

  if( !context )
    return;

  context->d_func_dynamic()->removeImportedChildContext(this);

  //DUChain::contextChanged(this, DUChainObserver::Removal, DUChainObserver::ImportedParentContexts, context);
}

QVector<DUContext*> DUContext::importedChildContexts() const
{
  ENSURE_CAN_READ

  return d_func()->m_importedChildContexts;
}

DUContext * DUContext::findContext( const SimpleCursor& position, DUContext* parent) const
{
  ENSURE_CAN_READ

  if (!parent)
    parent = const_cast<DUContext*>(this);

  foreach (DUContext* context, parent->childContexts())
    if (context->range().contains(position)) {
      DUContext* ret = findContext(position, context);
      if (!ret)
        ret = context;

      return ret;
    }

  return 0;
}

bool DUContext::parentContextOf(DUContext* context) const
{
  if (this == context)
    return true;

  foreach (DUContext* child, childContexts())
    if (child->parentContextOf(context))
      return true;

  return false;
}

QList<Declaration*> DUContext::allLocalDeclarations(const Identifier& identifier) const
{
  ENSURE_CAN_READ
  QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);
  DUCHAIN_D(DUContext);
  QList<Declaration*> ret;

  QHash<Identifier, DeclarationPointer>::const_iterator it = d->m_localDeclarationsHash.find(identifier);
  QHash<Identifier, DeclarationPointer>::const_iterator end = d->m_localDeclarationsHash.end();

  for( ; it != end && it.key() == identifier; ++it )
    ret << (*it).data();

  return ret;
}

QList< QPair<Declaration*, int> > DUContext::allDeclarations(const SimpleCursor& position, const TopDUContext* topContext, bool searchInParents) const
{
  ENSURE_CAN_READ

  QList< QPair<Declaration*, int> > ret;

  QHash<const DUContext*, bool> hadContexts;
  // Iterate back up the chain
  mergeDeclarationsInternal(ret, position, hadContexts, topContext ? topContext : this->topContext(), searchInParents);

  return ret;
}

const QVector<Declaration*> DUContext::localDeclarations() const
{
  ENSURE_CAN_READ

  QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);
  return d_func()->m_localDeclarations;
}

void DUContext::mergeDeclarationsInternal(QList< QPair<Declaration*, int> >& definitions, const SimpleCursor& position, QHash<const DUContext*, bool>& hadContexts, const TopDUContext* source, bool searchInParents, int currentDepth) const
{
  DUCHAIN_D(DUContext);
  if(hadContexts.contains(this))
    return;
  hadContexts[this] = true;

  if( (type() == DUContext::Namespace || type() == DUContext::Global) && currentDepth < 1000 )
    currentDepth += 1000;

  {
    QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);
      foreach (DeclarationPointer decl, d->m_localDeclarationsHash)
        if ( decl && (!position.isValid() || decl->range().start <= position) )
          definitions << qMakePair(decl.data(), currentDepth);
  }

  QVectorIterator<Import> it = d->m_importedContexts;
  it.toBack();
  while (it.hasPrevious()) {
    const Import& import(it.previous());
    DUContext* context = import.context.data();
    while( !context && it.hasPrevious() )
      context = it.previous().context.data();
    if( !context )
      break;

    if( position.isValid() && import.position.isValid() && position < import.position )
      continue;

    context->mergeDeclarationsInternal(definitions, SimpleCursor::invalid(), hadContexts, source, false, currentDepth+1);
  }

  if (searchInParents && parentContext())                            ///Only respect the position if the parent-context is not a class(@todo this is language-dependent)
    parentContext()->mergeDeclarationsInternal(definitions, position, hadContexts, source, true, currentDepth+1);
}

void DUContext::deleteLocalDeclarations()
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);
  QVector<Declaration*> declarations;
  {
    QMutexLocker lock(&DUContextData::m_localDeclarationsMutex);
    declarations = d->m_localDeclarations;
  }

  qDeleteAll(declarations);
  Q_ASSERT(d->m_localDeclarations.isEmpty());
}

void DUContext::deleteChildContextsRecursively()
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);
  QVector<DUContext*> children = d->m_childContexts;
  qDeleteAll(children);

  Q_ASSERT(d->m_childContexts.isEmpty());
}

QVector< Declaration * > DUContext::clearLocalDeclarations( )
{
  QVector< Declaration * > ret = localDeclarations();
  foreach (Declaration* dec, ret)
    dec->setContext(0);
  return ret;
}

QualifiedIdentifier DUContext::scopeIdentifier(bool includeClasses) const
{
  DUCHAIN_D(DUContext);
  ENSURE_CAN_READ

  QualifiedIdentifier ret;
  d->scopeIdentifier(includeClasses, ret);

  return ret;
}

bool DUContext::equalScopeIdentifier(const DUContext* rhs) const
{
  ENSURE_CAN_READ

  const DUContext* left = this;
  const DUContext* right = rhs;

  while(left || right) {
    if(!left || !right)
      return false;

    if(left->d_func()->m_scopeIdentifier != right->d_func()->m_scopeIdentifier)
      return false;

    left = left->parentContext();
    right = right->parentContext();
  }

  return true;
}

void DUContext::setLocalScopeIdentifier(const QualifiedIdentifier & identifier)
{
  ENSURE_CAN_WRITE
  //Q_ASSERT(d_func()->m_childContexts.isEmpty() && d_func()->m_localDeclarations.isEmpty());
  bool wasInSymbolTable = inSymbolTable();
  setInSymbolTable(false);
  d_func_dynamic()->m_scopeIdentifier = identifier;
  setInSymbolTable(wasInSymbolTable);
  //DUChain::contextChanged(this, DUChainObserver::Change, DUChainObserver::Identifier);
}

const QualifiedIdentifier & DUContext::localScopeIdentifier() const
{
  //ENSURE_CAN_READ Commented out for performance reasons

  return d_func()->m_scopeIdentifier;
}

DUContext::ContextType DUContext::type() const
{
  //ENSURE_CAN_READ This is disabled, because type() is called very often while searching, and it costs us performance

  return d_func()->m_contextType;
}

void DUContext::setType(ContextType type)
{
  ENSURE_CAN_WRITE

  d_func_dynamic()->m_contextType = type;

  //DUChain::contextChanged(this, DUChainObserver::Change, DUChainObserver::ContextType);
}

QList<Declaration*> DUContext::findDeclarations(const Identifier& identifier, const SimpleCursor& position, const TopDUContext* topContext, SearchFlags flags) const
{
  ENSURE_CAN_READ

  DeclarationList ret;
  SearchItem::PtrList identifiers;
  identifiers << SearchItem::Ptr(new SearchItem(QualifiedIdentifier(identifier)));
  findDeclarationsInternal(identifiers, position.isValid() ? position : range().end, AbstractType::Ptr(), ret, topContext ? topContext : this->topContext(), flags);
  return arrayToList(ret);
}

void DUContext::deleteUse(int index)
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);
  d->m_uses.remove(index);

  if(!d->m_rangesForUses.isEmpty()) {
    if(d->m_rangesForUses[index]) {
      EditorIntegrator editor;
      editor.setCurrentUrl(url());
      LockedSmartInterface iface = editor.smart();
      if (iface) {
        d->m_rangesForUses[index]->removeWatcher(this);
        EditorIntegrator::releaseRange(d->m_rangesForUses[index]);
      }
    }
    d->m_rangesForUses.remove(index);
  }
}

void DUContext::deleteUses()
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);

  d->m_uses.clear();

  clearUseSmartRanges();
}

bool DUContext::inDUChain() const {
  if( d_func()->m_anonymousInParent )
    return false;

  TopDUContext* top = topContext();
  return top && top->inDuChain();
}

SimpleCursor DUContext::importPosition(const DUContext* target) const
{
  ENSURE_CAN_READ
  DUCHAIN_D(DUContext);
  for(int a = 0; a < d->m_importedContexts.size(); ++a)
    if(d->m_importedContexts[a].context.data() == target)
      return d->m_importedContexts[a].position;
  return SimpleCursor::invalid();
}

QVector<DUContext::Import> DUContext::importedParentContexts() const
{
  ENSURE_CAN_READ

  return d_func()->m_importedContexts;
}

QList<DUContext*> DUContext::findContexts(ContextType contextType, const QualifiedIdentifier& identifier, const SimpleCursor& position, SearchFlags flags) const
{
  ENSURE_CAN_READ

  QList<DUContext*> ret;
  SearchItem::PtrList identifiers;
  identifiers << SearchItem::Ptr(new SearchItem(identifier));

  findContextsInternal(contextType, identifiers, position.isValid() ? position : range().end, ret, flags);
  return ret;
}

void DUContext::applyAliases(const SearchItem::PtrList& baseIdentifiers, SearchItem::PtrList& identifiers, const SimpleCursor& position, bool canBeNamespace) const {

  QList<Declaration*> imports = allLocalDeclarations(globalImportIdentifier);

  FOREACH_ARRAY( const SearchItem::Ptr& identifier, baseIdentifiers ) {
    bool addUnmodified = true;

    if( !identifier->isExplicitlyGlobal ) {

      if( !imports.isEmpty() )
      {
        //We have namespace-imports.
        foreach( Declaration* importDecl, imports )
        {
          if( importDecl->range().end > position )
            continue;
          //Search for the identifier with the import-identifier prepended
          Q_ASSERT(dynamic_cast<NamespaceAliasDeclaration*>(importDecl));
          NamespaceAliasDeclaration* alias = static_cast<NamespaceAliasDeclaration*>(importDecl);
          identifiers.append( SearchItem::Ptr( new SearchItem( alias->importIdentifier(), identifier ) ) ) ;
        }
      }

      if( !identifier->isEmpty() && (identifier->hasNext() || canBeNamespace) ) {
        QList<Declaration*> aliases = allLocalDeclarations(identifier->identifier);
        if(!aliases.isEmpty()) {
          //The first part of the identifier has been found as a namespace-alias.
          //In c++, we only need the first alias. However, just to be correct, follow them all for now.
          foreach( Declaration* aliasDecl, aliases )
          {
            if( aliasDecl->range().end > position )
              continue;
            if(!dynamic_cast<NamespaceAliasDeclaration*>(aliasDecl))
              continue;

            addUnmodified = false; //The un-modified identifier can be ignored, because it will be replaced with the resolved alias
            NamespaceAliasDeclaration* alias = static_cast<NamespaceAliasDeclaration*>(aliasDecl);

            //Create an identifier where namespace-alias part is replaced with the alias target
            identifiers.append( SearchItem::Ptr( new SearchItem( alias->importIdentifier(), identifier->next ) ) ) ;
          }
        }
      }
    }

    if( addUnmodified )
        identifiers.append(identifier);
  }
}

void DUContext::applyUpwardsAliases(SearchItem::PtrList& identifiers) const {

  if(type() == Namespace) {
    QualifiedIdentifier localId = d_func()->m_scopeIdentifier;
    if(localId.isEmpty())
      return;

    //Make sure we search for the items in all namespaces of the same name, by duplicating each one with the namespace-identifier prepended.
    //We do this by prepending items to the current identifiers that equal the local scope identifier.
    SearchItem::Ptr newItem( new SearchItem(localId) );

    //This will exclude explictly global identifiers
    newItem->addToEachNode( identifiers );

    if(!newItem->next.isEmpty()) {
      //Prepend the full scope before newItem
      DUContext* parent = d_func()->m_parentContext.data();
      while(parent) {
        newItem = SearchItem::Ptr( new SearchItem(parent->d_func()->m_scopeIdentifier, newItem) );
        parent = parent->d_func()->m_parentContext.data();
      }

      newItem->isExplicitlyGlobal = true;
      insertToArray(identifiers, newItem, 0);
    }
  }
}

void DUContext::findContextsInternal(ContextType contextType, const SearchItem::PtrList& baseIdentifiers, const SimpleCursor& position, QList<DUContext*>& ret, SearchFlags flags) const
{
  DUCHAIN_D(DUContext);
  if (contextType == type()) {
    FOREACH_ARRAY( const SearchItem::Ptr& identifier, baseIdentifiers )
      if (identifier->match(scopeIdentifier(true)) && (!parentContext() || !identifier->isExplicitlyGlobal) )
        ret.append(const_cast<DUContext*>(this));
  }
  ///@todo This doesn't seem quite correct: Local Contexts aren't found anywhere
  ///Step 1: Apply namespace-aliases and -imports
  SearchItem::PtrList aliasedIdentifiers;
  //Because of namespace-imports and aliases, this identifier may need to be searched as under multiple names
  applyAliases(baseIdentifiers, aliasedIdentifiers, position, contextType == Namespace);

  if( !d->m_importedContexts.isEmpty() ) {
    ///Step 2: Give identifiers that are not marked as explicitly-global to imported contexts(explicitly global ones are treatead in TopDUContext)
    SearchItem::PtrList nonGlobalIdentifiers;
    FOREACH_ARRAY( const SearchItem::Ptr& identifier, aliasedIdentifiers )
      if( !identifier->isExplicitlyGlobal )
        nonGlobalIdentifiers << identifier;

    if( !nonGlobalIdentifiers.isEmpty() ) {
      QVectorIterator<Import> it = d->m_importedContexts;
      it.toBack();
      while (it.hasPrevious()) {
        DUContext* context = it.previous().context.data();

        while( !context && it.hasPrevious() ) {
          context = it.previous().context.data();
        }
        if( !context )
          break;

        context->findContextsInternal(contextType, nonGlobalIdentifiers, url() == context->url() ? position : context->range().end, ret, flags | InImportedParentContext);
      }
    }
  }

  ///Step 3: Continue search in parent
  if ( !(flags & DontSearchInParent) && shouldSearchInParent(flags) && parentContext()) {
    applyUpwardsAliases(aliasedIdentifiers);
    parentContext()->findContextsInternal(contextType, aliasedIdentifiers, url() == parentContext()->url() ? position : parentContext()->range().end, ret, flags);
  }
}

bool DUContext::shouldSearchInParent(SearchFlags flags) const
{
  return !(flags & InImportedParentContext);
}

const QVector< Use > & DUContext::uses() const
{
  ENSURE_CAN_READ

  synchronizeUsesFromSmart();
  return d_func()->m_uses;
}

int DUContext::createUse(int declarationIndex, const SimpleRange& range, KTextEditor::SmartRange* smartRange, int insertBefore)
{
  DUCHAIN_D_DYNAMIC(DUContext);
  ENSURE_CAN_WRITE

  if(insertBefore == -1) {
    //Find position where to insert
    int a = 0;
    for(; a < d->m_uses.size() && range.start > d->m_uses[a].m_range.start; ++a) { ///@todo do binary search
    }
    insertBefore = a;
  }

  d->m_uses.insert(insertBefore, Use(range, declarationIndex));
  if(smartRange) {
    Q_ASSERT(d->m_rangesForUses.size() == d->m_uses.size()-1);
    d->m_rangesForUses.insert(insertBefore, smartRange);
    smartRange->addWatcher(this);
//     smartRange->setWantsDirectChanges(true);

    d->m_uses[insertBefore].m_range = SimpleRange(*smartRange);
  }else{
    Q_ASSERT(d->m_rangesForUses.isEmpty());
  }

  return insertBefore;
}

KTextEditor::SmartRange* DUContext::useSmartRange(int useIndex)
{
  ENSURE_CAN_READ
  if(d_func()->m_rangesForUses.isEmpty())
    return 0;
  else{
    Q_ASSERT(useIndex >= 0 && useIndex < d_func()->m_rangesForUses.size());
    return d_func()->m_rangesForUses.at(useIndex);
  }
}


void DUContext::setUseSmartRange(int useIndex, KTextEditor::SmartRange* range)
{
  ENSURE_CAN_WRITE
  if(d_func()->m_rangesForUses.isEmpty())
      d_func()->m_rangesForUses.insert(0, d_func()->m_uses.size(), 0);

  Q_ASSERT(d_func()->m_rangesForUses.size() == d_func()->m_uses.size());

  if(d_func()->m_rangesForUses[useIndex]) {
    EditorIntegrator editor;
    editor.setCurrentUrl(url());
    LockedSmartInterface iface = editor.smart();
    if (iface) {
      d_func()->m_rangesForUses[useIndex]->removeWatcher(this);
      EditorIntegrator::releaseRange(d_func()->m_rangesForUses[useIndex]);
    }
  }

  d_func()->m_rangesForUses[useIndex] = range;
  d_func()->m_uses[useIndex].m_range = SimpleRange(*range);
  range->addWatcher(this);
//   range->setWantsDirectChanges(true);
}

void DUContext::clearUseSmartRanges()
{
  ENSURE_CAN_WRITE

  if (!d_func()->m_rangesForUses.isEmpty()) {
    EditorIntegrator editor;
    editor.setCurrentUrl(url());
    LockedSmartInterface iface = editor.smart();
    if (iface) {
      foreach (SmartRange* range, d_func()->m_rangesForUses) {
        range->removeWatcher(this);
        EditorIntegrator::releaseRange(range);
      }
    }

    d_func()->m_rangesForUses.clear();
  }
}

void DUContext::setUseDeclaration(int useNumber, int declarationIndex)
{
  ENSURE_CAN_WRITE
  d_func()->m_uses[useNumber].m_declarationIndex = declarationIndex;
}


DUContext * DUContext::findContextAt(const SimpleCursor & position) const
{
  ENSURE_CAN_READ

  if (!range().contains(position))
    return 0;

  foreach (DUContext* child, d_func()->m_childContexts)
    if (DUContext* specific = child->findContextAt(position))
      return specific;

  return const_cast<DUContext*>(this);
}

DUContext* DUContext::findContextIncluding(const SimpleRange& range) const
{
  ENSURE_CAN_READ

  if (!this->range().contains(range))
    return 0;

  foreach (DUContext* child, d_func()->m_childContexts)
    if (DUContext* specific = child->findContextIncluding(range))
      return specific;

  return const_cast<DUContext*>(this);
}

int DUContext::findUseAt(const SimpleCursor & position) const
{
  ENSURE_CAN_READ

  synchronizeUsesFromSmart();

  if (!range().contains(position))
    return -1;

  for(int a = 0; a < d_func()->m_uses.size(); ++a)
    if (d_func()->m_uses[a].m_range.contains(position))
      return a;

  return -1;
}

bool DUContext::inSymbolTable() const
{
  return d_func()->m_inSymbolTable;
}

void DUContext::setInSymbolTable(bool inSymbolTable)
{
  if(!d_func()->m_scopeIdentifier.isEmpty()) {
    if(!d_func()->m_inSymbolTable && inSymbolTable)
      SymbolTable::self()->addContext(this);
    else if(d_func()->m_inSymbolTable && !inSymbolTable)
      SymbolTable::self()->removeContext(this);
  }

  d_func_dynamic()->m_inSymbolTable = inSymbolTable;
}

// kate: indent-width 2;

void DUContext::clearImportedParentContexts()
{
  ENSURE_CAN_WRITE
  DUCHAIN_D_DYNAMIC(DUContext);
  foreach (Import parent, d->m_importedContexts)
      if( parent.context.data() )
        removeImportedParentContext(parent.context.data());

  Q_ASSERT(d->m_importedContexts.isEmpty());
}

void DUContext::cleanIfNotEncountered(const QSet<DUChainBase*>& encountered)
{
  ENSURE_CAN_WRITE

  foreach (Declaration* dec, localDeclarations())
    if (!encountered.contains(dec))
      delete dec;

  foreach (DUContext* childContext, childContexts())
    if (!encountered.contains(childContext))
      delete childContext;
}

TopDUContext* DUContext::topContext() const
{
  DUCHAIN_D(DUContext);
  if (d->m_parentContext.data())
    return d->m_parentContext.data()->topContext();

  return 0;
}

QWidget* DUContext::createNavigationWidget(Declaration* /*decl*/, TopDUContext* /*topContext*/, const QString& /*htmlPrefix*/, const QString& /*htmlSuffix*/) const
{
  return 0;
}

void DUContext::squeeze()
{
  DUCHAIN_D_DYNAMIC(DUContext);

  if(!d->m_uses.isEmpty())
    d->m_uses.squeeze();

  if(!d->m_importedContexts.isEmpty())
    d->m_importedContexts.squeeze();

  if(!d->m_childContexts.isEmpty())
    d->m_childContexts.squeeze();

  if(!d->m_importedChildContexts.isEmpty())
    d->m_importedChildContexts.squeeze();

  if(!d->m_localDeclarations.isEmpty())
    d->m_localDeclarations.squeeze();

  if(!d->m_rangesForUses.isEmpty())
    d->m_rangesForUses.squeeze();

  foreach(DUContext* child, childContexts())
    child->squeeze();
}

QList<SimpleRange> allUses(DUContext* context, int declarationIndex)
{
  QList<SimpleRange> ret;
  foreach(const Use& use, context->uses())
    if(use.m_declarationIndex == declarationIndex)
      ret << use.m_range;

  foreach(DUContext* child, context->childContexts())
    ret += allUses(child, declarationIndex);

  return ret;
}

QList<KTextEditor::SmartRange*> allSmartUses(DUContext* context, int declarationIndex)
{
  QList<KTextEditor::SmartRange*> ret;

  const QVector<Use>& uses(context->uses());

  for(int a = 0; a < uses.size(); ++a)
    if(uses[a].m_declarationIndex == declarationIndex) {
      KTextEditor::SmartRange* range = context->useSmartRange(a);
      if(range)
        ret << range;
    }

  foreach(DUContext* child, context->childContexts())
    ret += allSmartUses(child, declarationIndex);

  return ret;
}

DUContext::SearchItem::SearchItem(const QualifiedIdentifier& id, Ptr nextItem, int start) : isExplicitlyGlobal(start == 0 ? id.explicitlyGlobal() : false) {
  if(!id.isEmpty()) {
    if(id.count() > start)
      identifier = id.at(start);

    if(id.count() > start+1)
      addNext(Ptr( new SearchItem(id, nextItem, start+1) ));
    else if(nextItem)
      next.append(nextItem);
  }else if(nextItem) {
    ///If there is no prefix, just copy nextItem
    isExplicitlyGlobal = nextItem->isExplicitlyGlobal;
    identifier = nextItem->identifier;
    next = nextItem->next;
  }
}

DUContext::SearchItem::SearchItem(const QualifiedIdentifier& id, const PtrList& nextItems, int start) : isExplicitlyGlobal(start == 0 ? id.explicitlyGlobal() : false) {
  if(id.count() > start)
    identifier = id.at(start);

  if(id.count() > start+1)
    addNext(Ptr( new SearchItem(id, nextItems, start+1) ));
  else
    next = nextItems;
}

DUContext::SearchItem::SearchItem(bool explicitlyGlobal, Identifier id, const PtrList& nextItems) : isExplicitlyGlobal(explicitlyGlobal), identifier(id), next(nextItems) {
}

DUContext::SearchItem::SearchItem(bool explicitlyGlobal, Identifier id, Ptr nextItem) : isExplicitlyGlobal(explicitlyGlobal), identifier(id) {
  next.append(nextItem);
}

bool DUContext::SearchItem::match(const QualifiedIdentifier& id, int offset) const {
  if(id.isEmpty()) {
    if(identifier.isEmpty() && next.isEmpty())
      return true;
    else
      return false;
  }

  if(id.at(offset) != identifier) //The identifier is different
    return false;

  if(offset == id.count()-1) {
    if(next.isEmpty())
      return true; //match
    else
      return false; //id is too short
  }

  for(int a = 0; a < next.size(); ++a)
    if(next[a]->match(id, offset+1))
      return true;

  return false;
}

bool DUContext::SearchItem::isEmpty() const {
  return identifier.isEmpty();
}

bool DUContext::SearchItem::hasNext() const {
  return !next.isEmpty();
}

QList<QualifiedIdentifier> DUContext::SearchItem::toList(const QualifiedIdentifier& prefix) const {
  QList<QualifiedIdentifier> ret;

  QualifiedIdentifier id = prefix;
  if(id.isEmpty())
  id.setExplicitlyGlobal(isExplicitlyGlobal);
  if(!identifier.isEmpty())
    id.push(identifier);

  if(next.isEmpty()) {
    ret << id;
  } else {
    for(int a = 0; a < next.size(); ++a)
      ret += next[a]->toList(id);
  }
  return ret;
}


void DUContext::SearchItem::addNext(SearchItem::Ptr other) {
  next.append(other);
}

void DUContext::SearchItem::addToEachNode(SearchItem::Ptr other) {
  if(other->isExplicitlyGlobal)
    return;

  next.append(other);
  for(int a = 0; a < next.size()-1; ++a)
    next[a]->addToEachNode(other);
}

void DUContext::SearchItem::addToEachNode(SearchItem::PtrList other) {
  int added = 0;
  FOREACH_ARRAY(SearchItem::Ptr o, other) {
    if(!o->isExplicitlyGlobal) {
      next.append(o);
      ++added;
    }
  }

  for(int a = 0; a < next.size()-added; ++a)
    next[a]->addToEachNode(other);
}

DUContext::Import::Import(DUContext* _context, const SimpleCursor& _position) : context(_context), position(_position) {
}

}

KDevelop::DUContext::SearchItem::PtrList& operator<<(KDevelop::DUContext::SearchItem::PtrList& list, const KDevelop::DUContext::SearchItem::Ptr& item) {
  list.append(item);
  return list;
}

void KDevelop::insertToArray(KDevelop::DUContext::SearchItem::PtrList& array, KDevelop::DUContext::SearchItem::Ptr item, int position) {
  Q_ASSERT(position >= 0 && position <= array.size());
  array.resize(array.size()+1);
  for(int a = array.size()-1; a > position; --a) {
    array[a] = array[a-1];
  }
  array[position] = item;
}

// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
