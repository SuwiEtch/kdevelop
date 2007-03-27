/**

  	 Copyright (C) 2003-2005 Mickael Marchand <marchand@kde.org>

	 This program is free software; you can redistribute it and/or
	 modify it under the terms of the GNU General Public
	 License as published by the Free Software Foundation; either
	 version 2 of the License, or (at your option) any later version.

	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	 General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program; see the file COPYING.  If not, write to
	 the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
	 Boston, MA 02111-1307, USA.
	 */

#ifndef __SUBVERSION_CORE_H__
#define __SUBVERSION_CORE_H__

#include <qobject.h>
#include <qwidget.h>
#include <kio/job.h>
#include <kurl.h>
#include <ktempdir.h>
#include "subversion_fileinfo.h"

class KDevProject;
class subversionPart;
class subversionWidget;
class KApplication;
class SvnBlameHolder;
class SvnLogHolder;
class SvnLogViewWidget;

// class subversionCore : public QObject, public DCOPObject
class subversionCore : public QObject {
  Q_OBJECT
//   K_DCOP

public:

  subversionCore(subversionPart *part);
  ~subversionCore();
//	subversionWidget *processWidget() const;
	SvnLogViewWidget *processWidget() const;
	void update( const KURL::List&);
	void commit( const KURL::List&, bool recurse, bool keeplocks );
	void svnLog( const KURL::List& list,
			int revstart, QString revKindStart, int revend, QString revKindEnd, 
			bool repositLog, bool discorverChangedPath, bool strictNodeHistory );
	void blame( const KURL &url, bool repositBlame, int revstart, QString revKindStart, int revend, QString revKindEnd );
	void add( const KURL::List&);
	void del( const KURL::List&);
	void diff( const KURL::List&, const QString& where);
	void revert( const KURL::List&);
	void resolve( const KURL::List&);
	void checkout();
	void createNewProject( const QString& dirName, const KURL& importURL, bool init );
    KDevVCSFileInfoProvider *fileInfoProvider() const;

// k_dcop:
// 	void notification( const QString&, int,int, const QString&, int,int ,long int, const QString& );

private slots:
	void slotEndCheckout( KIO::Job * job );
	void slotResult( KIO::Job * job );
	void slotLogResult( KIO::Job * job );
	void slotBlameResult( KIO::Job * job );

signals:
	void checkoutFinished( QString dir );

private:

// 	QGuardedPtr<subversionWidget> m_widget;
	subversionPart *m_part;
	QString wcPath;
	SVNFileInfoProvider *m_fileInfoProvider;
	QStringList diffresult; //for diff commands ;)
	// be nice about tmp diff files: delete all of them when exiting.
	KTempDir* diffTmpDir;

	QGuardedPtr<SvnLogViewWidget> m_logViewWidget;
	QValueList<SvnLogHolder> holderList;
	QValueList<SvnBlameHolder> blameList;
// 	QMap<unsigned int, SvnBlameHolder> blameList;
	
};

#endif
