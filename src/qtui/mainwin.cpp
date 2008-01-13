/***************************************************************************
 *   Copyright (C) 2005-08 by the Quassel Project                          *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
//#define SPUTDEV

#include "mainwin.h"

#include "bufferview.h"
#include "chatline-old.h"
#include "client.h"
#include "coreconnectdlg.h"
#include "networkmodel.h"
#include "buffermodel.h"
#include "nicklistwidget.h"
#include "serverlist.h"
#include "settingsdlg.h"
#include "signalproxy.h"
#include "topicwidget.h"
#include "uisettings.h"

#include "selectionmodelsynchronizer.h"
#include "mappedselectionmodel.h"

#include "settingspages/fontssettingspage.h"
#include "settingspages/identitiessettingspage.h"

#include "debugconsole.h"

MainWin::MainWin(QtUi *_gui, QWidget *parent) : QMainWindow(parent), gui(_gui) {
  ui.setupUi(this);
  setWindowTitle("Quassel IRC");
  //setWindowTitle(QString::fromUtf8("Κυασελ Εγαρζη"));
  setWindowIcon(QIcon(":icons/quassel-icon.png"));
  setWindowIconText("Quassel IRC");

  statusBar()->showMessage(tr("Waiting for core..."));
  serverListDlg = new ServerListDlg(this);
  settingsDlg = new SettingsDlg(this);
  debugConsole = new DebugConsole(this);
}

void MainWin::init() {
  Client::signalProxy()->attachSignal(this, SIGNAL(requestBacklog(BufferInfo, QVariant, QVariant)));
  ui.bufferWidget->init();

  show();

  //QVariantMap connInfo;
  //connInfo["User"] = "Default";
  //connInfo["Password"] = "password";
  //connectToCore(connInfo);

  statusBar()->showMessage(tr("Not connected to core."));
  systray = new QSystemTrayIcon(this);
  systray->setIcon(QIcon(":/icons/quassel-icon.png"));
  systray->show();

  //setupSettingsDlg();

  setupMenus();
  setupViews();
  setupSettingsDlg();

  // create nick dock
  nickDock = new QDockWidget(tr("Nicks"), this);
  nickDock->setObjectName("NickDock");
  nickDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

  nickListWidget = new NickListWidget(nickDock);
  nickDock->setWidget(nickListWidget);

  addDockWidget(Qt::RightDockWidgetArea, nickDock);
  ui.menuViews->addAction(nickDock->toggleViewAction());

  // restore mainwin state
  UiSettings s;
  restoreState(s.value("MainWinState").toByteArray());

  disconnectedFromCore();  // Disable menus and stuff
  showCoreConnectionDlg(true); // autoconnect if appropriate
  //ui.actionConnectCore->activate(QAction::Trigger);

  if(serverListDlg->showOnStartup()) {
    showServerList();
  }
  
  setDockNestingEnabled(true);
  
  
  // new Topic Stuff... should be probably refactored out into a separate method
  
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);

  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);


  QDockWidget *dock = new QDockWidget(tr("Topic Dock"), this);
  dock->setObjectName("TopicDock");
  dock->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);

  TopicWidget *topicwidget = new TopicWidget(dock);
  dock->setWidget(topicwidget);

  Client::bufferModel()->mapProperty(1, Qt::DisplayRole, topicwidget, "topic");

  addDockWidget(Qt::TopDockWidgetArea, dock);

  ui.menuViews->addAction(dock->toggleViewAction());


  // attach the BufferWidget to the PropertyMapper
  Client::bufferModel()->mapProperty(0, NetworkModel::BufferIdRole, ui.bufferWidget, "currentBuffer");
  connect(Client::networkModel(), SIGNAL(bufferAboutToBeRemoved(BufferId)),
	  ui.bufferWidget, SLOT(removeBuffer(BufferId)));

  // attach the NickList to the PropertyMapper
  Client::bufferModel()->mapProperty(0, NetworkModel::BufferIdRole, nickListWidget, "currentBuffer");
  
  
#ifdef SPUTDEV
  showSettingsDlg();
#endif

}

MainWin::~MainWin() {
  //typedef QHash<QString, Buffer*> BufHash;
  //foreach(BufHash h, buffers.values()) {
  //  foreach(Buffer *b, h.values()) {
  //    delete b;
  //  }
  //}
  //foreach(Buffer *buf, buffers.values()) delete buf;
}

/* This is implemented in settingspages.cpp */
/*
void MainWin::setupSettingsDlg() {

}
*/

void MainWin::setupMenus() {
  connect(ui.actionConnectCore, SIGNAL(triggered()), this, SLOT(showCoreConnectionDlg()));
  connect(ui.actionDisconnectCore, SIGNAL(triggered()), Client::instance(), SLOT(disconnectFromCore()));
  connect(ui.actionNetworkList, SIGNAL(triggered()), this, SLOT(showServerList()));
  connect(ui.actionEditIdentities, SIGNAL(triggered()), serverListDlg, SLOT(editIdentities()));
  connect(ui.actionSettingsDlg, SIGNAL(triggered()), this, SLOT(showSettingsDlg()));
  connect(ui.actionDebug_Console, SIGNAL(triggered()), this, SLOT(showDebugConsole()));
  connect(ui.actionAboutQt, SIGNAL(triggered()), QApplication::instance(), SLOT(aboutQt()));
}

void MainWin::setupViews() {
  BufferModel *model = Client::bufferModel();

  addBufferView(tr("All Buffers"), model, BufferViewFilter::AllNets, QList<uint>());
  addBufferView(tr("All Channels"), model, BufferViewFilter::AllNets|BufferViewFilter::NoQueries|BufferViewFilter::NoServers, QList<uint>());
  addBufferView(tr("All Queries"), model, BufferViewFilter::AllNets|BufferViewFilter::NoChannels|BufferViewFilter::NoServers, QList<uint>());
  addBufferView(tr("All Networks"), model, BufferViewFilter::AllNets|BufferViewFilter::NoChannels|BufferViewFilter::NoQueries, QList<uint>());
  addBufferView(tr("Full Custom"), model, BufferViewFilter::FullCustom, QList<uint>());

  ui.menuViews->addSeparator();
}

void MainWin::addBufferView(const QString &viewname, QAbstractItemModel *model, const BufferViewFilter::Modes &mode, const QList<uint> &nets) {
  QDockWidget *dock = new QDockWidget(viewname, this);
  dock->setObjectName(QString("ViewDock-" + viewname)); // should be unique for mainwindow state!
  dock->setAllowedAreas(Qt::RightDockWidgetArea|Qt::LeftDockWidgetArea);

  //create the view and initialize it's filter
  BufferView *view = new BufferView(dock);
  view->show();
  view->setFilteredModel(model, mode, nets);
  Client::bufferModel()->synchronizeView(view);
  dock->setWidget(view);
  dock->show();

  addDockWidget(Qt::LeftDockWidgetArea, dock);
  ui.menuViews->addAction(dock->toggleViewAction());

  netViews.append(dock);
}

void MainWin::setupSettingsDlg() {
  settingsDlg->registerSettingsPage(new FontsSettingsPage(settingsDlg));
  settingsDlg->registerSettingsPage(new IdentitiesSettingsPage(settingsDlg));
}

void MainWin::connectedToCore() {
  foreach(BufferInfo id, Client::allBufferInfos()) {
    emit requestBacklog(id, 1000, -1);
  }

  ui.menuViews->setEnabled(true);
  ui.menuCore->setEnabled(true);
  ui.actionConnectCore->setEnabled(false);
  ui.actionDisconnectCore->setEnabled(true);
  ui.actionNetworkList->setEnabled(true);
  ui.bufferWidget->show();
  statusBar()->showMessage(tr("Connected to core."));
}

void MainWin::disconnectedFromCore() {
  ui.menuViews->setEnabled(false);
  ui.menuCore->setEnabled(false);
  ui.actionDisconnectCore->setEnabled(false);
  ui.actionNetworkList->setEnabled(false);
  ui.bufferWidget->hide();
  ui.actionConnectCore->setEnabled(true);
  nickListWidget->reset();
  statusBar()->showMessage(tr("Not connected to core."));
}

AbstractUiMsg *MainWin::layoutMsg(const Message &msg) {
  return new ChatLine(msg);
}

void MainWin::showCoreConnectionDlg(bool autoConnect) {
  coreConnectDlg = new CoreConnectDlg(this, autoConnect);
  connect(coreConnectDlg, SIGNAL(finished(int)), this, SLOT(coreConnectionDlgFinished(int)));
  coreConnectDlg->setModal(true);
  if(!autoConnect || !coreConnectDlg->willDoInternalAutoConnect())
    coreConnectDlg->show(); // avoid flicker and show dlg only if we do remote connect, which needs a progress bar
  if(autoConnect) coreConnectDlg->doAutoConnect();
}

void MainWin::coreConnectionDlgFinished(int /*code*/) {

  coreConnectDlg->close();
}


void MainWin::showServerList() {
//  if(!serverListDlg) {
//    serverListDlg = new ServerListDlg(this);
//  }
  serverListDlg->show();
  serverListDlg->raise();
}

void MainWin::showSettingsDlg() {
  settingsDlg->show();
}

void MainWin::showDebugConsole() {
  debugConsole->show();
}

void MainWin::closeEvent(QCloseEvent *event)
{
  //if (userReallyWantsToQuit()) {
    ui.bufferWidget->saveState();
    UiSettings s;
    s.setValue("MainWinSize", size());
    s.setValue("MainWinPos", pos());
    s.setValue("MainWinState", saveState());
    delete systray;
    event->accept();
  //} else {
    //event->ignore();
  //}
}

