/* sctp_graph_arwn_dialog.h
 *
 * $Id: sctp_graph_arwnd_dialog.h 54200 2013-12-17 22:03:37Z morriss $
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef SCTP_GRAPH_ARWND_DIALOG_H
#define SCTP_GRAPH_ARWND_DIALOG_H

#include "config.h"
#include "qcustomplot.h"
#include <glib.h>

#include <file.h>
#include <math.h>
#include <epan/dissectors/packet-sctp.h>
#include "epan/packet.h"

#include "ui/tap-sctp-analysis.h"

#include <QDialog>
#include <QObject>
#include <QMessageBox>

namespace Ui {
class SCTPGraphArwndDialog;
}


class SCTPGraphArwndDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SCTPGraphArwndDialog(QWidget *parent = 0, sctp_assoc_info_t *assoc = NULL, capture_file *cf = NULL, int dir = 0);
    ~SCTPGraphArwndDialog();

public slots:
    void setCaptureFile(capture_file *cf) { cap_file_ = cf; }

private slots:
    void on_pushButton_4_clicked();

    void graphClicked(QCPAbstractPlottable* plottable, QMouseEvent* event);

    void on_saveButton_clicked();

private:
    Ui::SCTPGraphArwndDialog *ui;
    sctp_assoc_info_t     *selected_assoc;
    capture_file *cap_file_;
    int frame_num;
    int direction;
    int startArwnd;
    QVector<double> xa, ya;
    QVector<guint32> fa;
 //   QVector<QString> typeStrings;

    void drawGraph();
    void drawArwndGraph();
};

#endif // SCTP_GRAPH_DIALOG_H

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
