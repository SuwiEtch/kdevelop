/* $Id$
 *
 *  Copyright (C) 2002 Roberto Raggi (raggi@cli.di.unipi.it)
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
#ifndef CCODETEMPLATEOPTS_H
#define CCODETEMPLATEOPTS_H

#include <qwidget.h>
#include <codetemplateoptsdlg.h>

/**
  *@author kdevelop-team
  */

class CCodeTemplateOpts : public CodeTemplateOptsDlg  {
    Q_OBJECT
public:
    CCodeTemplateOpts(QWidget *parent=0, const char *name=0);
    ~CCodeTemplateOpts();

public slots:
    void slotSettingsChanged();

protected:
    void slotAddTemplate();
    void slotRemoveTemplate();
    void slotSelectionChanged();
    void slotTextChanged();
};

#endif
