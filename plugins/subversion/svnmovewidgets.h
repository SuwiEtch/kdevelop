/***************************************************************************
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef KDEVPLATFORM_PLUGIN_SVN_MOVEWIDGETS_H
#define KDEVPLATFORM_PLUGIN_SVN_MOVEWIDGETS_H

#include <kdialog.h>
#include "svnmodels.h"
#include "ui_moveoptiondlg.h"

class QUrl;

class SvnMoveOptionDlg : public KDialog
{
    Q_OBJECT
public:
    explicit SvnMoveOptionDlg( const QUrl &reqUrl, SvnInfoHolder *info, QWidget *parent );
    ~SvnMoveOptionDlg();

    QUrl source();
    QUrl dest();
    bool force();

private Q_SLOTS:
    void srcAsUrlClicked();
    void srcAsPathClicked();

private:
    Ui::SvnMoveOptionDlg ui;
    QUrl m_reqUrl;
    SvnInfoHolder *m_info;
};

#endif
