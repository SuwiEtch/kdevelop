/*
 *  Copyright (C) 2007 Dukju Ahn (dukjuahn@gmail.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 *
 */

#ifndef SVNLOGVIEWWIDGET_H
#define SVNLOGVIEWWIDGET_H

#include "svn_logviewwidgetbase.h"
#include "svn_logviewoptiondlgbase.h"
#include "subversion_part.h"
#include "subversion_widget.h"
#include <qvaluelist.h>
#include <qlistview.h>

class SvnLogHolder{
	public:
		SvnLogHolder(){};
		~SvnLogHolder(){};
		QString author;
		QString date;
		QString logMsg;
		QString pathList;
		QString rev;
};

class SvnLogViewWidget : public SvnLogViewWidgetBase {
	Q_OBJECT
public:
	SvnLogViewWidget(subversionPart *part, QWidget *parent);
	~SvnLogViewWidget();
	void setLogResult( QValueList<SvnLogHolder> *loglist );
	void append(QString txt);
public slots:
	void slotClicked( QListViewItem* item );
private:
	subversionPart *m_part;
	bool connected;
};

class SvnLogViewItem : public SvnIntSortListItem {
public:
	SvnLogViewItem( QListView* parent );
	~SvnLogViewItem();
	
	QString m_pathList;
	QString m_message;
};

class SvnLogViewOptionDlg : public SvnLogViewOptionDlgBase {
	Q_OBJECT
public:
	SvnLogViewOptionDlg(QWidget *parent=0, const char* name=0, bool modal=TRUE, WFlags f=0);
	~SvnLogViewOptionDlg();
	int revstart();
	QString revKindStart();
	int revend();
	QString revKindEnd();
	bool repositLog();
	bool strictNode();
public slots:
	void reinstallRevisionSpecifiers(int repositState );
	void setStartRevnumRadio();
	void setStartRevkindRadio();
	void setEndRevnumRadio();
	void setEndRevkindRadio();
};

#endif

