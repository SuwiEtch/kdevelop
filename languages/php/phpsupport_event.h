/*
   Copyright (C) 2003 by Roberto Raggi <roberto@kdevelop.org>
   Copyright (C) 2005 by Nicolas Escuder <n.escuder@intra-links.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   version 2, License as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#ifndef __phpsupport_events_h
#define __phpsupport_events_h

#include <qevent.h>
#include <qvaluelist.h>

#if QT_VERSION < 0x030100
#include <kdevmutex.h>
#else
#include <qmutex.h>
#endif

enum
{
    Event_FileParsed = QEvent::User + 1000
};

class FileParsedEvent: public QCustomEvent
{
public:
    FileParsedEvent( const QString& fileName, const QValueList<Action>& actions )
    : QCustomEvent(Event_FileParsed), m_fileName( fileName )
    {
        QValueListConstIterator<Action> it = actions.begin();
        while (it != actions.end()) {
            Action p = *it;
            m_actions.append(Action(p.text(), p.args(), p.line(), p.column(), p.level()));
            ++it;
        }
    }

    QString fileName() const { return m_fileName; }
    QValueList<Action> actions() const { return m_actions; }

private:
    QString m_fileName;
    QValueList<Action> m_actions;

private:
    FileParsedEvent( const FileParsedEvent& source );
    void operator = ( const FileParsedEvent& source );
};


#endif // __phpsupport_events_h
