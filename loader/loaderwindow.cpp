/*
 * Common Public Attribution License Version 1.0. 
 * 
 * The contents of this file are subject to the Common Public Attribution 
 * License Version 1.0 (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License 
 * at http://www.xTuple.com/CPAL.  The License is based on the Mozilla 
 * Public License Version 1.1 but Sections 14 and 15 have been added to 
 * cover use of software over a computer network and provide for limited 
 * attribution for the Original Developer. In addition, Exhibit A has 
 * been modified to be consistent with Exhibit B.
 * 
 * Software distributed under the License is distributed on an "AS IS" 
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See 
 * the License for the specific language governing rights and limitations 
 * under the License. 
 * 
 * The Original Code is xTuple ERP: PostBooks Edition
 * 
 * The Original Developer is not the Initial Developer and is __________. 
 * If left blank, the Original Developer is the Initial Developer. 
 * The Initial Developer of the Original Code is OpenMFG, LLC, 
 * d/b/a xTuple. All portions of the code written by xTuple are Copyright 
 * (c) 1999-2008 OpenMFG, LLC, d/b/a xTuple. All Rights Reserved. 
 * 
 * Contributor(s): ______________________.
 * 
 * Alternatively, the contents of this file may be used under the terms 
 * of the xTuple End-User License Agreeement (the xTuple License), in which 
 * case the provisions of the xTuple License are applicable instead of 
 * those above.  If you wish to allow use of your version of this file only 
 * under the terms of the xTuple License and not to allow others to use 
 * your version of this file under the CPAL, indicate your decision by 
 * deleting the provisions above and replace them with the notice and other 
 * provisions required by the xTuple License. If you do not delete the 
 * provisions above, a recipient may use your version of this file under 
 * either the CPAL or the xTuple License.
 * 
 * EXHIBIT B.  Attribution Information
 * 
 * Attribution Copyright Notice: 
 * Copyright (c) 1999-2008 by OpenMFG, LLC, d/b/a xTuple
 * 
 * Attribution Phrase: 
 * Powered by xTuple ERP: PostBooks Edition
 * 
 * Attribution URL: www.xtuple.org 
 * (to be included in the "Community" menu of the application if possible)
 * 
 * Graphic Image as provided in the Covered Code, if any. 
 * (online at www.xtuple.com/poweredby)
 * 
 * Display of Attribution Information is required in Larger Works which 
 * are defined in the CPAL as a work which combines Covered Code or 
 * portions thereof with code not governed by the terms of the CPAL.
 */

#include "loaderwindow.h"

#include <QApplication>
#include <QDomDocument>
#include <QFileDialog>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QRegExp>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimerEvent>

#include <gunzip.h>
#include <loadappscript.h>
#include <loadappui.h>
#include <loadcmd.h>
#include <loadimage.h>
#include <loadpriv.h>
#include <loadreport.h>
#include <package.h>
#include <prerequisite.h>
#include <script.h>
#include <tarfile.h>
#include <xsqlquery.h>

#include "data.h"

#define DEBUG false

LoaderWindow::LoaderWindow(QWidget* parent, const char* name, Qt::WindowFlags fl)
    : QMainWindow(parent, fl)
{
  setupUi(this);
  setObjectName(name);

  (void)statusBar();

  _multitrans = false;
  _premultitransfile = false;
  _package = 0;
  _files = 0;
  _dbTimerId = startTimer(60000);
  fileNew();
}

LoaderWindow::~LoaderWindow()
{
  // no need to delete child widgets, Qt does it all for us
}

void LoaderWindow::languageChange()
{
  retranslateUi(this);
}

void LoaderWindow::fileNew()
{
  // we don't actually create files here but we are using this as the
  // stub to unload and properly setup the UI to respond correctly to
  // having no package currently loaded.
  if(_package != 0)
  {
    delete _package;
    _package = 0;
  }

  if(_files != 0)
  {
    delete _files;
    _files = 0;
  }

  _pkgname->setText(tr("No Package is currently loaded."));

  _status->clear();
  _status->setEnabled(false);

  _progress->setValue(0);
  _progress->setEnabled(false);

  _text->clear();
  _text->setEnabled(false);

  _start->setEnabled(false);
}


void LoaderWindow::fileOpen()
{
  fileNew();

  QString filename = QFileDialog::getOpenFileName(this);
  if(filename.isEmpty())
    return;

  QByteArray data = gunzipFile(filename);
  if(data.isEmpty())
  {
    QMessageBox::warning(this, tr("Error Opening File"),
                         tr("<p>The file %1 appears to be empty or it is not "
                            "compressed in the expected format.")
                         .arg(filename));
    return;
  }

  _files = new TarFile(data);
  if(!_files->isValid())
  {
    QMessageBox::warning(this, tr("Error Opening file"),
                         tr("<p>The file %1 does not appear to contain a valid "
                            "update package (not a valid TAR file?).")
                         .arg(filename));
    delete _files;
    _files = 0;
    return;
  }

  // find the content file
  QStringList list = _files->_list.keys();
  QString contentFile = QString::null;
  QRegExp re(".*contents.xml$");
  for(QStringList::Iterator mit = list.begin(); mit != list.end(); ++mit)
  {
    if(re.exactMatch(*mit))
    {
      if(!contentFile.isNull())
      {
        QMessageBox::warning(this, tr("Error Opening file"),
                             tr("<p>Multiple content.xml files found in %1. "
                                "Currently only packages containing a single "
                                "content.xml file are supported.")
                             .arg(filename));
        delete _files;
        _files = 0;
        return;
      }
      contentFile = *mit;
    }
  }

  if(contentFile.isNull())
  {
    QMessageBox::warning(this, tr("Error Opening file"),
                         tr("<p>No contents.xml file was found in package %1.")
                         .arg(filename));
    delete _files;
    _files = 0;
    return;
  }

  QByteArray docData = _files->_list[contentFile];
  QDomDocument doc;
  QString errMsg;
  int errLine, errCol;
  if(!doc.setContent(docData, &errMsg, &errLine, &errCol))
  {
    QMessageBox::warning(this, tr("Error Opening file"),
                         tr("<p>There was a problem reading the contents.xml "
                            "file in this package.<br>%1<br>Line %2, "
                            "Column %3").arg(errMsg).arg(errLine).arg(errCol));
    delete _files;
    _files = 0;
    return;
  }

  _package = new Package(doc.documentElement());
  _pkgname->setText(tr("Package %1 (%2)").arg(_package->id()).arg(filename));

  _progress->setValue(0);
  _progress->setMaximum(_files->_list.count() - 1);
  _progress->setEnabled(true);

  _text->clear();
  _text->setEnabled(true);

  _status->setEnabled(true);
  _status->setText(tr("<p><b>Checking Prerequisites!</b></p>"));
  _text->setText("<p><b>Prerequisites</b>:</p>");
  bool allOk = true;
  // check prereqs
  QString str;
  QStringList strlist;
  QStringList::Iterator slit;
  QSqlQuery qry;
  for(QList<Prerequisite>::iterator i = _package->_prerequisites.begin();
      i != _package->_prerequisites.end(); ++i)
  {
    Prerequisite p = *i;
    bool passed = false;
    _status->setText(tr("<p><b>Checking Prerequisites!</b></p><p>%1...</p>").arg(p.name()));
    _text->append(tr("<p>%1</p>").arg(p.name()));
    switch(p.type())
    {
      case Prerequisite::Query:
        qry.exec(p.query());
        passed = false;
        if(qry.first())
          passed = qry.value(0).toBool();

        if(!passed)
        {
          allOk = false;
          //QMessageBox::warning(this, tr("Prerequisite Not Met"), tr("The prerequisite %1 was not met.").arg(p.name()));

          str = tr("<p><blockquote><font size=\"+1\" color=\"red\"><b>Failed</b></font><br />");
          if(!p.message().isEmpty())
           str += tr("<p>%1</p>").arg(p.message());

          strlist = p.providerList();
          if(strlist.count() > 0)
          {
            str += tr("<b>Requires:</b><br />");
            str += tr("<ul>");
            for(slit = strlist.begin(); slit != strlist.end(); ++slit)
              str += tr("<li>%1: %2</li>").arg(p.provider(*slit).package()).arg(p.provider(*slit).info());
            str += tr("</ul>");
          }
          
          str += tr("</blockquote></blockquote></p>");
          _text->append(str);
        }
        break;
      default:
        QMessageBox::warning(this, tr("Unhandled Prerequisite"),
                             tr("<p>Encountered an unknown Prerequisite type. "
                                "Prerequisite '%1' has not been validated.")
                             .arg(p.name()));
    }
  }

  if(!allOk)
  {
    _status->setText(tr("<p><b>Checking Prerequisites!</b></p><p>One or more prerequisites <b>FAILED</b>. These prerequisites must be satisified before continuing.</p>"));
    return;
  }

  _status->setText(tr("<p><b>Checking Prerequisites!</b></p><p>Check completed.</p>"));
  _text->append(tr("<p><b><font color=\"green\">Ready to Start update!</font></b></p>"));
  _text->append(tr("<p><b>NOTE</b>: Have you backed up your database? If not, you should "
                   "backup your database now. It is good practice to backup a database "
                   "before updating it.</p>"));

  /*
  single vs multiple transaction functionality was added at around the same
  time as OpenMFG/PostBooks 2.3.0 was being developed. before 2.3.0, update
  scripts from xTuple (OpenMFG, LLC) assumed multiple transactions (one per
  file within the package). take advantage of the update package naming
  conventions to see if we've been given a pre-2.3.0 file and *need* to use
  multiple transactions.
  */
  _premultitransfile = false;
  QString destver = filename;
  // if follows OpenMFG/xTuple naming convention
  if (destver.contains(QRegExp(".*/?[12][0123][0-9]((alpha|beta|rc)[1-9])?"
			       "to"
			       "[1-9][0-9][0-9]((alpha|beta|rc)[1-9])?.gz$")))
  {
    if (DEBUG)
      qDebug("%s", destver.toAscii().data());
    destver.remove(QRegExp(".*/?[12][0123][0-9]((alpha|beta|rc)[1-9])?to"));
    if (DEBUG)
      qDebug("%s", destver.toAscii().data());
    destver.remove(QRegExp("((alpha|beta|rc)[1-9])?.gz$"));
    if (DEBUG)
      qDebug("%s", destver.toAscii().data());
    // now destver is just the destination release #
    if (destver.toInt() < 230)
      _premultitransfile = true;
  }
  else
  {
    if (DEBUG)
      qDebug("not one of our old files");
  }

  _start->setEnabled(true);
}


void LoaderWindow::fileExit()
{
  qApp->closeAllWindows();
}


void LoaderWindow::helpContents()
{
  launchBrowser(this, "http://wiki.xtuple.org/UpdaterDoc");
}

// copied from xtuple/guiclient/guiclient.cpp and made independent of Qt3Support
// TODO: put in a generic place and use both from there or use WebKit instead
void LoaderWindow::launchBrowser(QWidget * w, const QString & url)
{
#if defined(Q_OS_WIN32)
  // Windows - let the OS do the work
  QT_WA( {
      ShellExecute(w->winId(), 0, (TCHAR*)url.ucs2(), 0, 0, SW_SHOWNORMAL );
    } , {
      ShellExecuteA( w->winId(), 0, url.local8Bit(), 0, 0, SW_SHOWNORMAL );
    } );
#else
  QString b(getenv("BROWSER"));
  QStringList browser;
  if (! b.isEmpty())
    browser = b.split(':');

#if defined(Q_OS_MACX)
  browser.append("/usr/bin/open");
#else
  // append this on linux just as a good guess
  browser.append("/usr/bin/firefox");
  browser.append("/usr/bin/mozilla");
#endif
  for(QStringList::const_iterator i=browser.begin(); i!=browser.end(); ++i) {
    QString app = *i;
    if(app.contains("%s")) {
      app.replace("%s", url);
    } else {
      app += " " + url;
    }
    app.replace("%%", "%");
    QProcess *proc = new QProcess(w);
    QStringList args = app.split(QRegExp(" +"));
    QString appname = args.takeFirst();

    proc->start(appname, args);
    if (proc->waitForStarted() &&
        proc->waitForFinished())
      return;

    QMessageBox::warning(w, tr("Failed to open URL"),
                         tr("<p>Before you can run a web browser you must "
                            "set the environment variable BROWSER to point "
                            "to the browser executable.") );
  }
#endif  // if not windows
}

void LoaderWindow::helpAbout()
{
  QMessageBox::about(this, _name,
    tr("<p>Apply update packages to your xTuple ERP database."
       "<p>Version %1</p>"
       "<p>%2</p>"
       "All Rights Reserved")
    .arg(_version).arg(_copyright));
}

void LoaderWindow::timerEvent( QTimerEvent * e )
{
  if(e->timerId() == _dbTimerId)
  {
    QSqlDatabase db = QSqlDatabase::database(QSqlDatabase::defaultConnection,FALSE);
    if(db.isValid())
      QSqlQuery qry("SELECT CURRENT_DATE;");
    // if we are not connected then we have some problems!
  }
}


/*
 use _multitrans to see if the user requested a single transaction wrapped
 around the entire import
 but use _premultitransfile to see if we need multiple transactions
 even if the user requested one.
 */
void LoaderWindow::sStart()
{
  QString rollbackMsg(tr("<p><font color=red>The upgrade has been aborted due "
                         "to an error and your database was rolled back to the "
                         "state it was in when the upgrade was initiated."
                         "</font><br>"));
  _start->setEnabled(false);
  _text->setText("<p></p>");

  QString prefix = QString::null;
  if(!_package->id().isEmpty())
    prefix = _package->id() + "/";

  QSqlQuery qry;
  if(!_multitrans && !_premultitransfile)
    qry.exec("begin;");

  QString errMsg;
  int pkgid = -1;
  if (! _package->name().isEmpty())
  {
    pkgid = _package->writeToDB(errMsg);
    if (pkgid >= 0)
      _text->append(tr("Saving Package Header was successful."));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
  }

  _status->setText(tr("<p><b>Updating Privileges</b></p>"));
  _text->append(tr("<p>Loading new Privileges...</p>"));
  for(QList<LoadPriv>::iterator i = _package->_privs.begin();
      i != _package->_privs.end(); ++i)
  {
    LoadPriv priv = *i;
    if (priv.writeToDB(_package->name(), errMsg) >= 0)
      _text->append(tr("Import of %1 was successful.").arg(priv.name()));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
    _progress->setValue(_progress->value() + 1);
  }
  _text->append(tr("<p>Completed importing new Privileges.</p>"));

  // update scripts here
  _status->setText(tr("<p><b>Updating Schema</b></p>"));
  _text->append(tr("<p>Applying database change files...</p>"));
  Script script;
  for(QList<Script>::iterator i = _package->_scripts.begin();
      i != _package->_scripts.end(); ++i)
  {
    script = *i;

    QByteArray scriptData = _files->_list[prefix + script.name()];
    if(scriptData.isEmpty())
    {
      QMessageBox::warning(this, tr("File Missing"),
                           tr("<p>The file %1 is missing from this package.")
                           .arg(script.name()));
      continue;
    }

    QString sql(scriptData);

    bool again = true;
    int r = 0;
    while(again) {
      again = false;
      if(_multitrans || _premultitransfile)
        qry.exec("begin;");
      if(!qry.exec(sql))
      {
        QSqlError err = qry.lastError();
        QString message = tr("The following error was encountered while "
                             "trying to import %1 into the database:<br>\n"
                             "\t%2<br>\n\t%3")
                      .arg(script.name())
                      .arg(err.driverText())
                      .arg(err.databaseText());
        _text->append(tr("<p>"));
        if((_multitrans || _premultitransfile) && script.onError() == Script::Ignore)
          _text->append(tr("<font color=orange>%1</font><br>").arg(message));
        else
          _text->append(tr("<font color=red>%1</font><br>").arg(message));
        qry.exec("rollback;");
        if(!_multitrans && !_premultitransfile)
        {
          _text->append(rollbackMsg);
          return;
        }
        switch(script.onError())
        {
          case Script::Ignore:
            _text->append(tr("<font color=orange><b>IGNORING</b> the above "
                             "errors and skipping script %1.</font><br>")
                            .arg(script.name()));
            break;
          case Script::Stop:
          case Script::Prompt:
          case Script::Default:
          default:
            r = QMessageBox::question(this, tr("Encountered an Error"),
                  tr("%1.\n"
                     "Please select the action that you would like to take.").arg(message),
                  tr("Retry"), tr("Ignore"), tr("Abort"), 0, 0 );
            if(r == 0)
            {
              _text->append(tr("RETRYING..."));
              again = true;
            }
            else if(r == 1)
              _text->append(tr("<font color=orange><b>IGNORING</b> the above errors at user "
                               "request and skipping script %1.</font><br>")
                              .arg(script.name()) );
            else
              if(r == 2) return;
        }
      }
    }
    if(_multitrans || _premultitransfile)
      qry.exec("commit;");
    _progress->setValue(_progress->value() + 1);
  }

  _status->setText(tr("<p><b>Updating Report Definitions</b></p>"));
  _text->append(tr("<p>Loading new report definitions...</p>"));
  for(QList<LoadReport>::iterator i = _package->_reports.begin();
      i != _package->_reports.end(); ++i)
  {
    LoadReport report = *i;
    QByteArray data = _files->_list[prefix + report.name()];
    if(data.isEmpty())
    {
      QMessageBox::warning(this, tr("File Missing"),
                           tr("<p>The file %1 in this package is empty.").
                           arg(report.name()));
      continue;
    }
    if (report.writeToDB(data, _package->name(), errMsg) >= 0)
      _text->append(tr("Import of %1 was successful.").arg(report.name()));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
    _progress->setValue(_progress->value() + 1);
  }
  _text->append(tr("<p>Completed importing new report definitions.</p>"));

  _status->setText(tr("<p><b>Updating User Interface Definitions</b></p>"));
  _text->append(tr("<p>Loading User Interface definitions...</p>"));
  for(QList<LoadAppUI>::iterator i = _package->_appuis.begin();
      i != _package->_appuis.end(); ++i)
  {
    LoadAppUI appui = *i;
    QByteArray data = _files->_list[prefix + appui.name()];
    if(data.isEmpty())
    {
      QMessageBox::warning(this, tr("File Missing"),
                           tr("<p>The file %1 in this package is empty.").
                           arg(appui.name()));
      continue;
    }
    if (appui.writeToDB(data, _package->name(), errMsg) >= 0)
      _text->append(tr("Import of %1 was successful.").arg(appui.name()));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
    _progress->setValue(_progress->value() + 1);
  }
  _text->append(tr("<p>Completed importing User Interface definitions.</p>"));

  _status->setText(tr("<p><b>Updating Application Script Definitions</b></p>"));
  _text->append(tr("<p>Loading Application Script definitions...</p>"));
  for(QList<LoadAppScript>::iterator i = _package->_appscripts.begin();
      i != _package->_appscripts.end(); ++i)
  {
    LoadAppScript appscript = *i;
    QByteArray data = _files->_list[prefix + appscript.name()];
    if(data.isEmpty())
    {
      QMessageBox::warning(this, tr("File Missing"),
                           tr("<p>The file %1 in this package is empty.").
                           arg(appscript.name()));
      continue;
    }
    if (appscript.writeToDB(data, _package->name(), errMsg) >= 0)
      _text->append(tr("Import of %1 was successful.").arg(appscript.name()));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
    _progress->setValue(_progress->value() + 1);
  }
  _text->append(tr("<p>Completed importing Application Script definitions.</p>"));

  _status->setText(tr("<p><b>Updating Custom Commands</b></p>"));
  _text->append(tr("<p>Loading new Custom Commands...</p>"));
  for(QList<LoadCmd>::iterator i = _package->_cmds.begin();
      i != _package->_cmds.end(); ++i)
  {
    LoadCmd cmd = *i;
    if (cmd.writeToDB(_package->name(), errMsg) >= 0)
      _text->append(tr("Import of %1 was successful.").arg(cmd.name()));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
    _progress->setValue(_progress->value() + 1);
  }
  _text->append(tr("<p>Completed importing new Custom Commands.</p>"));

  _status->setText(tr("<p><b>Updating Image Definitions</b></p>"));
  _text->append(tr("<p>Loading Image definitions...</p>"));
  for(QList<LoadImage>::iterator i = _package->_images.begin();
      i != _package->_images.end(); ++i)
  {
    LoadImage image = *i;
    QByteArray data = _files->_list[prefix + image.name()];
    if(data.isEmpty())
    {
      QMessageBox::warning(this, tr("File Missing"),
                           tr("<p>The file %1 in this package is empty.").
                           arg(image.name()));
      continue;
    }
    if (image.writeToDB(data, _package->name(), errMsg) >= 0)
      _text->append(tr("Import of %1 was successful.").arg(image.name()));
    else
    {
      _text->append(errMsg);
      qry.exec("rollback;");
      if(!_multitrans && !_premultitransfile)
      {
        _text->append(rollbackMsg);
        return;
      }
    }
    _progress->setValue(_progress->value() + 1);
  }
  _text->append(tr("<p>Completed importing Image definitions.</p>"));

  _progress->setValue(_progress->value() + 1);

  if(!_multitrans && !_premultitransfile)
    qry.exec("commit;");

  _text->append(tr("<p>The Update is now complete!</p>"));
}

void LoaderWindow::setMultipleTransactions(bool mt)
{
  _multitrans = mt;
}
