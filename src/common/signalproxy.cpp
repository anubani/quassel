/***************************************************************************
 *   Copyright (C) 2005-07 by the Quassel IRC Team                         *
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

#include "signalproxy.h"
#include <QObject>
#include <QIODevice>
#include <QAbstractSocket>
#include <QHash>
#include <QMultiHash>
#include <QList>
#include <QSet>
#include <QDebug>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMutexLocker>
#include <QRegExp>
#include "util.h"

class SignalRelay: public QObject {

/* Q_OBJECT is not necessary or even allowed, because we implement
   qt_metacall ourselves (and don't use any other features of the meta
   object system)
*/
public:
  SignalRelay(SignalProxy* parent, QObject* source);
  int qt_metacall(QMetaObject::Call _c, int _id, void **_a);

  void attachSignal(int methodId, const QByteArray &func);

  void setSynchronize(bool);
  bool synchronize() const;
  
  int sigCount() const;
  
private:
  SignalProxy* proxy;
  QObject* caller;
  QMultiHash<int, QByteArray> sigNames;
  bool _sync;
};

SignalRelay::SignalRelay(SignalProxy* parent, QObject* source)
  : QObject(parent),
    proxy(parent),
    caller(source),
    _sync(false)
{
  QObject::connect(source, SIGNAL(destroyed()), parent, SLOT(detachSender()), Qt::DirectConnection);
}

int SignalRelay::qt_metacall(QMetaObject::Call _c, int _id, void **_a) {
  _id = QObject::qt_metacall(_c, _id, _a);
  if(_id < 0)
    return _id;
  if(_c == QMetaObject::InvokeMetaMethod) {
    if(sigNames.contains(_id) || synchronize()) {
      const QList<int> &argTypes = proxy->argTypes(caller, _id);
      QVariantList params;
      int n = argTypes.size();
      for(int i=0; i<n; i++)
        params.append(QVariant(argTypes[i], _a[i+1]));
      QMultiHash<int, QByteArray>::const_iterator funcIter = sigNames.constFind(_id);
      while(funcIter != sigNames.constEnd() && funcIter.key() == _id) {
        proxy->dispatchSignal(funcIter.value(), params);
        funcIter++;
      }
      
      // dispatch Sync Signal if necessary
      QByteArray signature(caller->metaObject()->method(_id).signature());
      if(synchronize() && proxy->syncMap(caller).contains(signature)) {
	// qDebug() << "__SYNC__ >>>"
	// 	 << caller->metaObject()->className()
	// 	 << caller->objectName()
	// 	 << proxy->methodName(caller, _id)
	// 	 << params;
	// params.prepend(QVariant(_id));
	params.prepend(signature);
	params.prepend(caller->objectName());
	params.prepend(caller->metaObject()->className());
	proxy->dispatchSignal((int)SignalProxy::Sync, params);
      }
    }
    _id -= 1;
  }
  return _id;
}

void SignalRelay::setSynchronize(bool sync) {
  const QMetaObject *meta = caller->metaObject();
  if(!_sync && sync) {
    // enable Sync
    for(int i = 0; i < meta->methodCount(); i++ ) {
      if(proxy->syncMap(caller).contains(meta->method(i).signature()))
	QMetaObject::connect(caller, i, this, QObject::staticMetaObject.methodCount() + i);
    }
  } else if (_sync && !sync) {
    // disable Sync
    for(int i = 0; i < meta->methodCount(); i++ ) {
      if(proxy->syncMap(caller).contains(meta->method(i).signature()))
	QMetaObject::disconnect(caller, i, this, QObject::staticMetaObject.methodCount() + i);
    }
  }
  _sync = sync;
}

bool SignalRelay::synchronize() const {
  return _sync;
}

int SignalRelay::sigCount() const {
  // only for debuging purpose
  return sigNames.count();
}

void SignalRelay::attachSignal(int methodId, const QByteArray &func) {
  // we ride without safetybelts here... all checking for valid method etc pp has to be done by the caller
  // all connected methodIds are offset by the standard methodCount of QObject
  if(!sigNames.contains(methodId))
    QMetaObject::connect(caller, methodId, this, QObject::staticMetaObject.methodCount() + methodId);

  QByteArray fn;
  if(!func.isEmpty()) {
    fn = QMetaObject::normalizedSignature(func);
  } else {
    fn = QByteArray("2") + caller->metaObject()->method(methodId).signature();
  }
  sigNames.insert(methodId, fn);
}
// ====================
// END SIGNALRELAY
// ====================


// ====================
//  SignalProxy
// ====================
SignalProxy::SignalProxy(QObject* parent)
  : QObject(parent)
{
  setProxyMode(Client);
}

SignalProxy::SignalProxy(ProxyMode mode, QObject* parent)
  : QObject(parent)
{
  setProxyMode(mode);
}

SignalProxy::SignalProxy(ProxyMode mode, QIODevice* device, QObject* parent)
  : QObject(parent)
{
  setProxyMode(mode);
  addPeer(device);
} 

SignalProxy::~SignalProxy() {
  QList<QObject*> senders = _relayHash.keys();
  foreach(QObject* sender, senders)
    detachObject(sender);
}

void SignalProxy::setProxyMode(ProxyMode mode) {
  foreach(QIODevice* peer, _peerByteCount.keys()) {
    if(peer->isOpen()) {
      qWarning() << "SignalProxy: Cannot change proxy mode while connected";
      return;
    }
  }
  _proxyMode = mode;
  if(mode == Server)
    initServer();
  else
    initClient();
}

SignalProxy::ProxyMode SignalProxy::proxyMode() const {
  return _proxyMode;
}

void SignalProxy::initServer() {
}

void SignalProxy::initClient() {
  attachSlot("__objectRenamed__", this, SLOT(objectRenamed(QByteArray, QString, QString)));
}

bool SignalProxy::addPeer(QIODevice* iodev) {
  if(!iodev)
    return false;
  
  if(_peerByteCount.contains(iodev))
    return true;

  if(proxyMode() == Client && !_peerByteCount.isEmpty()) {
    qWarning("SignalProxy: only one peer allowed in client mode!");
    return false;
  }

  if(!iodev->isOpen())
    qWarning("SignalProxy::the device you passed is not open!");

  connect(iodev, SIGNAL(disconnected()), this, SLOT(removePeerBySender()));
  connect(iodev, SIGNAL(readyRead()), this, SLOT(dataAvailable()));

  QAbstractSocket* sock  = qobject_cast<QAbstractSocket*>(iodev);
  if(sock) {
    connect(sock, SIGNAL(disconnected()), this, SLOT(removePeerBySender()));
  }

  _peerByteCount[iodev] = 0;

  if(_peerByteCount.count() == 1)
    emit connected();

  return true;
}

void SignalProxy::removePeerBySender() {
  // OK we're brutal here... but since it's a private slot we know what we've got connected to it...
  QIODevice *ioDev = (QIODevice *)(sender());
  removePeer(ioDev);
}

void SignalProxy::objectRenamed(QString oldname, QString newname) {
  const QMetaObject *meta = sender()->metaObject();
  const QByteArray className(meta->className());
  objectRenamed(className, oldname, newname);

  if(proxyMode() == Client)
    return;
  
  QVariantList params;
  params << className << oldname << newname;
  dispatchSignal("__objectRenamed__", params);
}

void SignalProxy::objectRenamed(QByteArray classname, QString oldname, QString newname) {
  QMutexLocker locker(&slaveMutex);
  if(_syncSlave.contains(classname) && _syncSlave[classname].contains(oldname) && oldname != newname)
    _syncSlave[classname][newname] = _syncSlave[classname].take(oldname);
}


void SignalProxy::removePeer(QIODevice* iodev) {
  if(_peerByteCount.isEmpty()) {
    qWarning() << "SignalProxy: No peers in use!";
    return;
  }

  if(proxyMode() == Server && !iodev) {
    // disconnect all
    QList<QIODevice *> peers = _peerByteCount.keys();
    foreach(QIODevice *peer, peers)
      removePeer(peer);
  }

  if(proxyMode() != Server && !iodev)
    iodev = _peerByteCount.keys().first();

  Q_ASSERT(iodev);

  if(!_peerByteCount.contains(iodev)) {
    qWarning() << "SignalProxy: unknown QIODevice" << iodev;
    return;
  }
     
  // take a last gasp
  while(true) {
    QVariant var;
    if(readDataFromDevice(iodev, _peerByteCount[iodev], var))
      receivePeerSignal(iodev, var);
    else
      break;
  }
  _peerByteCount.remove(iodev);

  disconnect(iodev, 0, this, 0);
  emit peerRemoved(iodev);

  if(_peerByteCount.isEmpty())
    emit disconnected();
}

void SignalProxy::setArgTypes(QObject* obj, int methodId) {
  const QMetaObject *meta = obj->metaObject();
  QList<QByteArray> p = meta->method(methodId).parameterTypes();
  QList<int> argTypes;
  int ct = p.count();
  for(int i=0; i<ct; i++)
    argTypes.append(QMetaType::type(p.value(i)));

  Q_ASSERT(!_classInfo[meta]->argTypes.contains(methodId));
  _classInfo[meta]->argTypes[methodId] = argTypes;
}

const QList<int> &SignalProxy::argTypes(QObject *obj, int methodId) {
  Q_ASSERT(_classInfo.contains(obj->metaObject()));
  if(!_classInfo[obj->metaObject()]->argTypes.contains(methodId))
    setArgTypes(obj, methodId);
  return _classInfo[obj->metaObject()]->argTypes[methodId];
}

void SignalProxy::setMethodName(QObject *obj, int methodId) {
  const QMetaObject *meta = obj->metaObject();
  QByteArray method(::methodName(meta->method(methodId)));
  Q_ASSERT(!_classInfo[meta]->methodNames.contains(methodId));
  _classInfo[meta]->methodNames[methodId] = method;
}

const QByteArray &SignalProxy::methodName(QObject *obj, int methodId) {
  Q_ASSERT(_classInfo.contains(obj->metaObject()));
  if(!_classInfo[obj->metaObject()]->methodNames.contains(methodId))
    setMethodName(obj, methodId);
  return _classInfo[obj->metaObject()]->methodNames[methodId];
}


void SignalProxy::setSyncMap(QObject *obj) {
  const QMetaObject *meta = obj->metaObject();
  QHash<QByteArray, int> syncMap;
  
  QList<int> slotIndexes;
  for(int i = 0; i < meta->methodCount(); i++) {
    if(meta->method(i).methodType() == QMetaMethod::Slot)
      slotIndexes << i;
  }

  QMetaMethod signal, slot;
  int matchIdx;
  for(int signalIdx = 0; signalIdx < meta->methodCount(); signalIdx++) {
    signal = meta->method(signalIdx);
    if(signal.methodType() != QMetaMethod::Signal)
      continue;

    matchIdx = -1;
    foreach(int slotIdx, slotIndexes) {
      slot = meta->method(slotIdx);
      if(methodsMatch(signal, slot)) {
	matchIdx = slotIdx;
	break;
      }
    }
    if(matchIdx != -1) {
      slotIndexes.removeAt(slotIndexes.indexOf(matchIdx));
      syncMap[QByteArray(signal.signature())] = matchIdx;
    }
  }

  Q_ASSERT(_classInfo[meta]->syncMap.isEmpty());
  _classInfo[meta]->syncMap = syncMap;
}

const QHash<QByteArray,int> &SignalProxy::syncMap(QObject *obj) {
  Q_ASSERT(_classInfo.contains(obj->metaObject()));
  if(_classInfo[obj->metaObject()]->syncMap.isEmpty())
    setSyncMap(obj);
  return _classInfo[obj->metaObject()]->syncMap;
}

void SignalProxy::createClassInfo(QObject *obj) {
  if(!_classInfo.contains(obj->metaObject()))
    _classInfo[obj->metaObject()] = new ClassInfo();
}

bool SignalProxy::attachSignal(QObject* sender, const char* signal, const QByteArray& sigName) {
  const QMetaObject* meta = sender->metaObject();
  QByteArray sig(meta->normalizedSignature(signal).mid(1));
  int methodId = meta->indexOfMethod(sig.constData());
  if(methodId == -1 || meta->method(methodId).methodType() != QMetaMethod::Signal) {
    qWarning() << "SignalProxy::attachSignal(): No such signal" << signal;
    return false;
  }

  createClassInfo(sender);

  SignalRelay* relay;
  if(_relayHash.contains(sender))
    relay = _relayHash[sender];
  else
    relay = _relayHash[sender] = new SignalRelay(this, sender);

  relay->attachSignal(methodId, sigName);

  return true;
}


bool SignalProxy::attachSlot(const QByteArray& sigName, QObject* recv, const char* slot) {
  const QMetaObject* meta = recv->metaObject();
  int methodId = meta->indexOfMethod(meta->normalizedSignature(slot).mid(1));
  if(methodId == -1 || meta->method(methodId).methodType() == QMetaMethod::Method) {
    qWarning() << "SignalProxy::attachSlot(): No such slot" << slot;
    return false;
  }

  createClassInfo(recv);

  QByteArray funcName = QMetaObject::normalizedSignature(sigName.constData());
  _attachedSlots.insert(funcName, qMakePair(recv, methodId));

  QObject::disconnect(recv, SIGNAL(destroyed()), this, SLOT(detachSender()));
  QObject::connect(recv, SIGNAL(destroyed()), this, SLOT(detachSender()), Qt::DirectConnection);
  return true;
}

void SignalProxy::synchronize(QObject *obj) {
  if(proxyMode() == Server)
    return synchronizeAsMaster(obj);
  else
    return synchronizeAsSlave(obj);
}

void SignalProxy::synchronizeAsMaster(QObject *sender) {
  createClassInfo(sender);

  SignalRelay* relay;
  if(_relayHash.contains(sender))
    relay = _relayHash[sender];
  else
    relay = _relayHash[sender] = new SignalRelay(this, sender);

  relay->setSynchronize(true);

  if(sender->metaObject()->indexOfSignal(QMetaObject::normalizedSignature("renameObject(QString, QString)")) != -1)
    connect(sender, SIGNAL(renameObject(QString, QString)), this, SLOT(objectRenamed(QString, QString)), Qt::DirectConnection);

  QByteArray className(sender->metaObject()->className());
  _syncSlave[className][sender->objectName()] = sender;

  setInitialized(sender);
}

void SignalProxy::synchronizeAsSlave(QObject *receiver) {
  QByteArray className(receiver->metaObject()->className());
  _syncSlave[className][receiver->objectName()] = receiver;
  
  createClassInfo(receiver);

  requestInit(receiver);
}

void SignalProxy::setInitialized(QObject *obj) {
  QMetaObject::invokeMethod(obj, "setInitialized");
}

bool SignalProxy::initialized(QObject *obj) {
  bool init;
  if(!QMetaObject::invokeMethod(obj, "initialized", Q_RETURN_ARG(bool, init)))
    init = false;
  return init;
}

void SignalProxy::requestInit(QObject *obj) {
  if(proxyMode() == Server || initialized(obj))
    return;

  QVariantList params;
  params << obj->metaObject()->className()
	 << obj->objectName();
  dispatchSignal((int)InitRequest, params);
}

void SignalProxy::detachSender() {
  // this is a private slot. the reason for that is, that we enfoce Qt::DirectConnection.
  // the result is, that we can be sure, that a registered receiver is removed immediately when it's destroyed.
  // since we're guarding the the internal datastructures with mutexes this means,
  // that the object destruction is deffered until we're finished delivering our sync/init/whatever request.
  // all in all: thread safety! *sigh*
  detachObject(sender());
}

// detachObject/Signals/Slots() can be called as a result of an incoming call
// this might destroy our the iterator used for delivery
// thus we wrap the actual disconnection by using QueuedConnections
void SignalProxy::detachObject(QObject* obj) {
  QMutexLocker locker(&slaveMutex);
  _detachSignals(obj);
  _detachSlots(obj);
  _stopSync(obj);
}

void SignalProxy::detachSignals(QObject* sender) {
  QMutexLocker locker(&slaveMutex);
  _detachSignals(sender);
}

void SignalProxy::detachSlots(QObject* receiver) {
  QMutexLocker locker(&slaveMutex);
  _detachSlots(receiver);
}

void SignalProxy::stopSync(QObject* obj) {
  QMutexLocker locker(&slaveMutex);
  _stopSync(obj);
}

void SignalProxy::_detachSignals(QObject* sender) {
  if(!_relayHash.contains(sender))
    return;
  _relayHash.take(sender)->deleteLater();
}

void SignalProxy::_detachSlots(QObject* receiver) {
  SlotHash::iterator slotIter = _attachedSlots.begin();
  while(slotIter != _attachedSlots.end()) {
    if(slotIter.value().first == receiver) {
      slotIter = _attachedSlots.erase(slotIter);
    } else
      slotIter++;
  }
}

void SignalProxy::_stopSync(QObject* obj) {
  if(_relayHash.contains(obj))
    _relayHash[obj]->setSynchronize(false);

  // we can't use a className here, since it might be effed up, if we receive the call as a result of a decon
  // gladly the objectName() is still valid. So we have only to iterate over the classes not each instance! *sigh*
  QHash<QByteArray, ObjectId>::iterator classIter = _syncSlave.begin();
  while(classIter != _syncSlave.end()) {
    if(classIter->contains(obj->objectName())) {
      classIter->remove(obj->objectName());
      break;
    }
    classIter++;
  }
}

void SignalProxy::dispatchSignal(QIODevice *receiver, const QVariant &identifier, const QVariantList &params) {
  QVariantList packedFunc;
  packedFunc << identifier;
  packedFunc << params;
  writeDataToDevice(receiver, QVariant(packedFunc));
}

void SignalProxy::dispatchSignal(const QVariant &identifier, const QVariantList &params) {
  // yes I know we have a little code duplication here... it's for the sake of performance
  QVariantList packedFunc;
  packedFunc << identifier;
  packedFunc << params;
  foreach(QIODevice* dev, _peerByteCount.keys())
    writeDataToDevice(dev, QVariant(packedFunc));
}

void SignalProxy::receivePeerSignal(QIODevice *sender, const QVariant &packedFunc) {
  QVariantList params(packedFunc.toList());

  QVariant call = params.takeFirst();
  if(call.type() != QVariant::Int)
    return handleSignal(call.toByteArray(), params);

  // well yes we are locking code here and not only data.
  // otherwise each handler would have to lock the data anyway. this leaves us on the safe side
  // unable to forget a lock
  QMutexLocker locker(&slaveMutex);

  switch(call.toInt()) {
  case Sync:
    return handleSync(params);
  case InitRequest:
    return handleInitRequest(sender, params);
  case InitData:
    return handleInitData(sender, params);
  default:
    qWarning() << "received undefined CallType" << call.toInt();
    return;
  }
}

void SignalProxy::handleSync(QVariantList params) {
  if(params.count() < 3) {
    qWarning() << "received invalid Sync call" << params;
    return;
  }
  
  QByteArray className = params.takeFirst().toByteArray();
  QString objectName = params.takeFirst().toString();
  QByteArray signal = params.takeFirst().toByteArray();

  if(!_syncSlave.contains(className) || !_syncSlave[className].contains(objectName)) {
    qWarning() << QString("no registered receiver for sync call: %s::%s (objectName=\"%s\"). Params are:").arg(QString(className)).arg(QString(signal)).arg(objectName)
	       << params;
    return;
  }

  QObject *receiver = _syncSlave[className][objectName];
  if(!syncMap(receiver).contains(signal)) {
    qWarning() << QString("no matching slot for sync call: %s::%s (objectName=\"%s\"). Params are:").arg(QString(className)).arg(QString(signal)).arg(objectName)
	       << params;
    return;
  }

  int slotId = syncMap(receiver)[signal];
  if(!invokeSlot(receiver, slotId, params))
    qWarning("SignalProxy::handleSync(): invokeMethod for \"%s\" failed ", methodName(receiver, slotId).constData());
}

void SignalProxy::handleInitRequest(QIODevice *sender, const QVariantList &params) {
  if(params.count() != 2) {
    qWarning() << "SignalProxy::handleInitRequest() received initRequest with invalid param Count:"
	       << params;
    return;
  }
  
  QByteArray className(params[0].toByteArray());
  QString objectName(params[1].toString());
  
  if(!_syncSlave.contains(className)) {
    qWarning() << "SignalProxy::handleInitRequest() received initRequest for unregistered Class:"
	       << className;
    return;
  }

  if(!_syncSlave[className].contains(objectName)) {
    qWarning() << "SignalProxy::handleInitRequest() received initRequest for unregistered Object:"
	       << className << objectName;
    return;
  }
  
  QObject *obj = _syncSlave[className][objectName];

  QVariantList params_;
  params_ << obj->metaObject()->className()
	  << obj->objectName()
	  << initData(obj);

  dispatchSignal(sender, (int)InitData, params_);
}

void SignalProxy::handleInitData(QIODevice *sender, const QVariantList &params) {
  Q_UNUSED(sender)
  if(params.count() != 3) {
    qWarning() << "SignalProxy::handleInitRequest() received initRequest with invalid param Count:"
	       << params;
    return;
  }
  
  QByteArray className(params[0].toByteArray());
  QString objectName(params[1].toString());
  QVariantMap propertyMap(params[2].toMap());

  if(!_syncSlave.contains(className)) {
    qWarning() << "SignalProxy::handleInitRequest() received initRequest for unregistered Class:"
	       << className;
    return;
  }

  if(!_syncSlave[className].contains(objectName)) {
    qWarning() << "SignalProxy::handleInitRequest() received initRequest for unregistered Object:"
	       << className << objectName;
    return;
  }

  QObject *obj = _syncSlave[className][objectName];
  setInitData(obj, propertyMap);
}

void SignalProxy::handleSignal(const QByteArray &funcName, const QVariantList &params) {
  QObject* receiver;
  int methodId;
  SlotHash::const_iterator slot = _attachedSlots.constFind(funcName);
  while(slot != _attachedSlots.constEnd() && slot.key() == funcName) {
    receiver = (*slot).first;
    methodId = (*slot).second;
    if(!invokeSlot(receiver, methodId, params))
      qWarning("SignalProxy::handleSignal(): invokeMethod for \"%s\" failed ", methodName(receiver, methodId).constData());
    slot++;
  }
}

bool SignalProxy::invokeSlot(QObject *receiver, int methodId, const QVariantList &params) {
  int numParams = argTypes(receiver, methodId).count();
  QGenericArgument args[9];
  for(int i = 0; i < numParams; i++)
    args[i] = QGenericArgument(params[i].typeName(), params[i].constData());
  return QMetaObject::invokeMethod(receiver, methodName(receiver, methodId), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
}

void SignalProxy::dataAvailable() {
  // yet again. it's a private slot. no need for checks.
  QIODevice* ioDev = qobject_cast<QIODevice* >(sender());
  QVariant var;
  while(readDataFromDevice(ioDev, _peerByteCount[ioDev], var))
    receivePeerSignal(ioDev, var);
}

void SignalProxy::writeDataToDevice(QIODevice *dev, const QVariant &item) {
  QAbstractSocket* sock  = qobject_cast<QAbstractSocket*>(dev);
  if(!dev->isOpen() || (sock && sock->state()!=QAbstractSocket::ConnectedState)) {
    qWarning("SignalProxy: Can't call on a closed device");
    return;
  }
  QByteArray block;
  QDataStream out(&block, QIODevice::WriteOnly);
  out.setVersion(QDataStream::Qt_4_2);
  out << (quint32)0 << item;
  out.device()->seek(0);
  out << (quint32)(block.size() - sizeof(quint32));
  dev->write(block);
}

bool SignalProxy::readDataFromDevice(QIODevice *dev, quint32 &blockSize, QVariant &item) {
  QDataStream in(dev);
  in.setVersion(QDataStream::Qt_4_2);

  if(blockSize == 0) {
    if(dev->bytesAvailable() < (int)sizeof(quint32)) return false;
    in >> blockSize;
  }

  if(dev->bytesAvailable() < blockSize)
    return false;
  in >> item;
  blockSize = 0;
  return true;
}

bool SignalProxy::methodsMatch(const QMetaMethod &signal, const QMetaMethod &slot) const {
  // if we don't even have the same basename it's a sure NO
  if(methodBaseName(signal) != methodBaseName(slot))
    return false;

  // are the signatures compatible?
  if(!QObject::staticMetaObject.checkConnectArgs(signal.signature(), slot.signature()))
    return false;

  // we take an educated guess if the signals and slots match
  QString signalsuffix = ::methodName(signal).mid(QString(::methodName(signal)).lastIndexOf(QRegExp("[A-Z]"))).toLower();
  QString slotprefix = ::methodName(slot).left(QString(::methodName(slot)).indexOf(QRegExp("[A-Z]"))).toLower();

  uint sizediff = qAbs(slotprefix.size() - signalsuffix.size());
  int ratio = editingDistance(slotprefix, signalsuffix) - sizediff;
  return (ratio < 2);
}

QString SignalProxy::methodBaseName(const QMetaMethod &method) {
  QString methodname = QString(method.signature()).section("(", 0, 0);

  // determine where we have to chop:
  if(method.methodType() == QMetaMethod::Slot) {
    // we take evertyhing from the first uppercase char if it's slot
    methodname = methodname.mid(methodname.indexOf(QRegExp("[A-Z]")));
  } else {
    // and if it's a signal we discard everything from the last uppercase char
    methodname = methodname.left(methodname.lastIndexOf(QRegExp("[A-Z]")));
  }

  methodname[0] = methodname[0].toUpper();

  return methodname;
}

QVariantMap SignalProxy::initData(QObject *obj) const {
  QVariantMap properties;

  const QMetaObject* meta = obj->metaObject();

  // we collect data from properties
  for(int i = 0; i < meta->propertyCount(); i++) {
    QMetaProperty prop = meta->property(i);
    properties[QString(prop.name())] = prop.read(obj);
  }

  // ...as well as methods, which have names starting with "init"
  for(int i = 0; i < meta->methodCount(); i++) {
    QMetaMethod method = meta->method(i);
    QString methodname(::methodName(method));
    if(!methodname.startsWith("init") || methodname.startsWith("initSet"))
      continue;

    QVariant value = QVariant(QVariant::nameToType(method.typeName()));
    QGenericReturnArgument genericvalue = QGenericReturnArgument(method.typeName(), &value);
    QMetaObject::invokeMethod(obj, methodname.toAscii(), genericvalue);

    properties[methodBaseName(method)] = value;
    // qDebug() << ">>> SYNC:" << methodBaseName(method) << value;
  }
  
  // properties["Payload"] = QByteArray(10000000, 'a');  // for testing purposes
  return properties;
}

void SignalProxy::setInitData(QObject *obj, const QVariantMap &properties) {
  if(initialized(obj))
    return;

  const QMetaObject *meta = obj->metaObject();
  
  QVariantMap::const_iterator iterator = properties.constBegin();
  while(iterator != properties.constEnd()) {
    QString name = iterator.key();
    int propertyIndex = meta->indexOfProperty(name.toAscii());

    if(propertyIndex == -1 || !meta->property(propertyIndex).isWritable())
      setInitValue(obj, name, iterator.value());
    else
      obj->setProperty(name.toAscii(), iterator.value());
    // qDebug() << "<<< SYNC:" << name << iterator.value();
    iterator++;
  }
  setInitialized(obj);
}

bool SignalProxy::setInitValue(QObject *obj, const QString &property, const QVariant &value) {
  QString handlername = QString("initSet") + property;
  handlername[7] = handlername[7].toUpper();
  QGenericArgument param(value.typeName(), value.constData());
  return QMetaObject::invokeMethod(obj, handlername.toAscii(), param);
}

void SignalProxy::dumpProxyStats() {
  QString mode;
  if(proxyMode() == Server)
    mode = "Server";
  else
    mode = "Client";

  int sigCount = 0;
  foreach(SignalRelay *relay, _relayHash.values())
    sigCount += relay->sigCount();

  int slaveCount = 0;
  foreach(ObjectId oid, _syncSlave.values())
    slaveCount += oid.count();
  
  qDebug() << this;
  qDebug() << "              Proxy Mode:" << mode;
  qDebug() << "attached sending Objects:" << _relayHash.count();
  qDebug() << "       number of Signals:" << sigCount;
  qDebug() << "          attached Slots:" << _attachedSlots.count();
  qDebug() << " number of synced Slaves:" << slaveCount;
  qDebug() << "number of Classes cached:" << _classInfo.count();
}

void SignalProxy::dumpSyncMap(QObject *object) {
  const QMetaObject *meta = object->metaObject();
  qDebug() << "SignalProxy: SyncMap for Class" << meta->className();

  QHash<QByteArray, int> syncMap_ = syncMap(object);
  QHash<QByteArray, int>::const_iterator iter = syncMap_.constBegin();
  while(iter != syncMap_.constEnd()) {
    qDebug() << iter.key() << "-->" << iter.value() << meta->method(iter.value()).signature();    
    iter++;
  }
//   QHash<int, int> syncMap_ = syncMap(object);
//   QHash<int, int>::const_iterator iter = syncMap_.constBegin();
//   while(iter != syncMap_.constEnd()) {
//     qDebug() << iter.key() << meta->method(iter.key()).signature() << "-->" << iter.value() << meta->method(iter.value()).signature();    
//     iter++;
//   }
}
