/***************************************************************************
 *   Copyright (C) 2005-07 by The Quassel Team                             *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
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

#include "global.h"
#include "bufferviewwidget.h"

/*****************************************
 *  Buffer Items stored in the Tree Model
 *****************************************/
TreeItem::TreeItem(QList<QVariant> &data, TreeItem *parent) {
  itemData = data;
  parentItem = parent;
  foreground = Qt::black;
}

TreeItem::TreeItem(TreeItem *parent) {
  itemData = QList<QVariant>();
  parentItem = parent;
  foreground = Qt::black;
}

TreeItem::~TreeItem() {
  qDeleteAll(childItems);
}

void TreeItem::appendChild(TreeItem *item) {
  childItems.append(item);
}

void TreeItem::removeChild(int row) {
  childItems.removeAt(row);
}

TreeItem *TreeItem::child(int row) {
  return childItems.value(row);
}

int TreeItem::childCount() const {
  return childItems.count();
}

int TreeItem::row() const {
  if(parentItem)
    return parentItem->childItems.indexOf(const_cast<TreeItem*>(this));
  else
    return 0;
}

TreeItem *TreeItem::parent() {
  return parentItem;
}

int TreeItem::columnCount() const {
  return itemData.count();
}

void TreeItem::setForeground(QColor newcolor) {
  foreground = newcolor;
}

QVariant TreeItem::data(int column, int role) const {
  switch(role) {
    case Qt::DisplayRole:
      if(column < itemData.count())
        return itemData[column];
      else
        return QVariant();

    case Qt::ForegroundRole:
      return foreground;
      
    default:
      return QVariant();
      
  }
}

/*****************************************
*  Fancy Buffer Items
*****************************************/
BufferTreeItem::BufferTreeItem(Buffer *buffer, TreeItem *parent) : TreeItem(parent) {
  buf = buffer;
  activity = Buffer::NoActivity;
}

void BufferTreeItem::setActivity(const Buffer::ActivityLevel &level) {
  activity = level;
}

QString BufferTreeItem::text(int column) const {
  switch(column) {
    case 0:
      return buf->displayName();
    case 1:
      return buf->networkName();
    default:
      return QString();
  }
}

QColor BufferTreeItem::foreground(int column) const {
  // for the time beeing we ignore the column :)
  if(activity & Buffer::Highlight) {
    return QColor(Qt::red);
  } else if(activity & Buffer::NewMessage) {
    return QColor(Qt::darkYellow);
  } else if(activity & Buffer::OtherActivity) {
    return QColor(Qt::darkGreen);
  } else {
    if(buf->isActive())
      return QColor(Qt::black);
    else
      return QColor(Qt::gray);
  }
}


QVariant BufferTreeItem::data(int column, int role) const {
  switch(role) {
    case Qt::DisplayRole:
      return text(column);
    case Qt::ForegroundRole:
      return foreground(column);
    case BufferTreeModel::BufferTypeRole:
      return buf->bufferType();
    case BufferTreeModel::BufferActiveRole:
      return buf->isActive();
    default:
      return QVariant();
  }
}

/*****************************************
 * BufferTreeModel
 *****************************************/
BufferTreeModel::BufferTreeModel(QObject *parent) : QAbstractItemModel(parent) {
  QList<QVariant> rootData;
  rootData << "Buffer" << "Network";
  rootItem = new TreeItem(rootData, 0);
  
  connect(this, SIGNAL(fakeUserInput(BufferId, QString)), guiProxy, SLOT(gsUserInput(BufferId, QString)));
}

BufferTreeModel::~BufferTreeModel() {
  delete rootItem;
}

QModelIndex BufferTreeModel::index(int row, int column, const QModelIndex &parent) const {
  if(!hasIndex(row, column, parent))
    return QModelIndex();
  
  TreeItem *parentItem;
  
  if(!parent.isValid())
    parentItem = rootItem;
  else
    parentItem = static_cast<TreeItem*>(parent.internalPointer());
  
  TreeItem *childItem = parentItem->child(row);
  if(childItem)
    return createIndex(row, column, childItem);
  else
    return QModelIndex();
}

QModelIndex BufferTreeModel::parent(const QModelIndex &index) const {
  if(!index.isValid())
    return QModelIndex();
  
  TreeItem *childItem = static_cast<TreeItem*>(index.internalPointer());
  TreeItem *parentItem = childItem->parent();
  
  if(parentItem == rootItem)
    return QModelIndex();
  
  return createIndex(parentItem->row(), 0, parentItem);
}

int BufferTreeModel::rowCount(const QModelIndex &parent) const {
  TreeItem *parentItem;
  if(parent.column() > 0)
    return 0;
  
  if(!parent.isValid())
    parentItem = rootItem;
  else
    parentItem = static_cast<TreeItem*>(parent.internalPointer());
  
  return parentItem->childCount();
}

int BufferTreeModel::columnCount(const QModelIndex &parent) const {
  if(parent.isValid())
    return static_cast<TreeItem*>(parent.internalPointer())->columnCount();
  else
    return rootItem->columnCount();
}

QVariant BufferTreeModel::data(const QModelIndex &index, int role) const {
  if(!index.isValid())
    return QVariant();

  TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
  return item->data(index.column(), role);
}

Qt::ItemFlags BufferTreeModel::flags(const QModelIndex &index) const {
  if(!index.isValid())
    return 0;

  // I think this is pretty ugly..
  if(isBufferIndex(index)) {
    Buffer *buffer = getBufferByIndex(index);
    if(buffer->bufferType() == Buffer::QueryBuffer)
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    else
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  } else {
    return Qt::ItemIsEnabled | Qt::ItemIsDropEnabled; 
  }
}

QVariant BufferTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    return rootItem->data(section, role);
  else
    return QVariant();
}

bool BufferTreeModel::removeRow(int row, const QModelIndex &parent) {
  beginRemoveRows(parent, row, row);
  TreeItem *item = static_cast<TreeItem*>(parent.internalPointer());
  item->removeChild(row);
  endRemoveRows();
  return true;
}

bool BufferTreeModel::isBufferIndex(const QModelIndex &index) const {
  return parent(index) != QModelIndex();
}

Buffer *BufferTreeModel::getBufferByIndex(const QModelIndex &index) const {
  BufferTreeItem *item = static_cast<BufferTreeItem *>(index.internalPointer());
  return item->buffer();
}

QModelIndex BufferTreeModel::getOrCreateNetworkItemIndex(Buffer *buffer) {
  QString net = buffer->networkName();
  
  if(networkItem.contains(net)) {
    return index(networkItem[net]->row(), 0);
  } else {
    QList<QVariant> data;
    data << net << "";
    
    int nextRow = rootItem->childCount();
    
    beginInsertRows(QModelIndex(), nextRow, nextRow);
    rootItem->appendChild(new TreeItem(data, rootItem));
    endInsertRows();
    
    networkItem[net] = rootItem->child(nextRow);
    return index(nextRow, 0);
  }
}

QModelIndex BufferTreeModel::getOrCreateBufferItemIndex(Buffer *buffer) {
  QModelIndex networkItemIndex = getOrCreateNetworkItemIndex(buffer);
  
  if(bufferItem.contains(buffer)) {
    return index(bufferItem[buffer]->row(), 0, networkItemIndex);
  } else {
    // first we determine the parent of the new Item
    TreeItem *networkItem = static_cast<TreeItem*>(networkItemIndex.internalPointer());

    int nextRow = networkItem->childCount();
    
    beginInsertRows(networkItemIndex, nextRow, nextRow);
    networkItem->appendChild(new BufferTreeItem(buffer, networkItem));
    endInsertRows();
                         
    bufferItem[buffer] = static_cast<BufferTreeItem *>(networkItem->child(nextRow));
    return index(nextRow, 0, networkItemIndex);
  }
}

QStringList BufferTreeModel::mimeTypes() const {
  QStringList types;
  types << "application/Quassel/BufferItem/row"
    << "application/Quassel/BufferItem/network";
  return types;
}

QMimeData *BufferTreeModel::mimeData(const QModelIndexList &indexes) const {
  QMimeData *mimeData = new QMimeData();

  QModelIndex index = indexes.first();
  
  mimeData->setData("application/Quassel/BufferItem/row", QByteArray::number(index.row()));
  mimeData->setData("application/Quassel/BufferItem/network", getBufferByIndex(index)->networkName().toUtf8());
  return mimeData;
}

bool BufferTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) {
  int sourcerow = data->data("application/Quassel/BufferItem/row").toInt();
  QString network = QString::fromUtf8(data->data("application/Quassel/BufferItem/network"));
  
  if(!networkItem.contains(network))
    return false;

  if(!isBufferIndex(parent)) // dropping at a network -> no merging needed
    return false;

  Buffer *sourceBuffer = static_cast<BufferTreeItem *>(networkItem[network]->child(sourcerow))->buffer();
  Buffer *targetBuffer = getBufferByIndex(parent);
  
  qDebug() << "merging" << sourceBuffer->bufferName() << "with" << targetBuffer->bufferName();
  bufferItem.remove(getBufferByIndex(parent));
  removeRow(parent.row(), BufferTreeModel::parent(parent));
  
  return true;
}

void BufferTreeModel::bufferUpdated(Buffer *buffer) {
  QModelIndex itemindex = getOrCreateBufferItemIndex(buffer);
  emit invalidateFilter();
  emit dataChanged(itemindex, itemindex);
}

// This Slot indicates that the user has selected a different buffer in the gui
void BufferTreeModel::changeCurrent(const QModelIndex &current, const QModelIndex &previous) {
  if(isBufferIndex(current)) {
    currentBuffer = getBufferByIndex(current);
    bufferActivity(Buffer::NoActivity, currentBuffer);
    emit bufferSelected(currentBuffer);
  }
}

// we received a double click on a buffer, so we're going to join it
void BufferTreeModel::doubleClickReceived(const QModelIndex &clicked) {
  if(isBufferIndex(clicked)) {
    Buffer *buffer = getBufferByIndex(clicked);
    if(!buffer->isStatusBuffer()) 
      emit fakeUserInput(buffer->bufferId(), QString("/join " + buffer->bufferName()));
  }
    
}

void BufferTreeModel::bufferActivity(Buffer::ActivityLevel level, Buffer *buffer) {
  if(bufferItem.contains(buffer) and buffer != currentBuffer)
    bufferItem[buffer]->setActivity(level);
  else
    bufferItem[buffer]->setActivity(Buffer::NoActivity);
  bufferUpdated(buffer);
}

void BufferTreeModel::selectBuffer(Buffer *buffer) {
  QModelIndex index = getOrCreateBufferItemIndex(buffer);
  emit updateSelection(index, QItemSelectionModel::ClearAndSelect);
}


/*****************************************
 * This Widget Contains the BufferView
 *****************************************/
BufferViewWidget::BufferViewWidget(QWidget *parent) : QWidget(parent) {
  ui.setupUi(this);
}

QSize BufferViewWidget::sizeHint() const {
  return QSize(150,100);
}


/*****************************************
 * Dock and API for the BufferViews
 *****************************************/
BufferViewDock::BufferViewDock(QAbstractItemModel *model, QString viewname, BufferViewFilter::Modes mode, QStringList nets, QWidget *parent) : QDockWidget(parent) {
  setObjectName(QString("View-" + viewname)); // should be unique for mainwindow state!
  setWindowTitle(viewname);

  BufferViewWidget *viewWidget = new BufferViewWidget(this);
  viewWidget->treeView()->setFilteredModel(model, mode, nets);
  viewWidget->treeView()->init();
  setWidget(viewWidget);
}
