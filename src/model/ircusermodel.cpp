/*
* Copyright (C) 2008-2013 The Communi Project
*
* This library is free software; you can redistribute it and/or modify it
* under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This library is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
* License for more details.
*/

#include "ircusermodel.h"
#include "ircusermodel_p.h"
#include "ircbuffermodel.h"
#include "ircconnection.h"
#include "ircchannel_p.h"
#include "ircuser.h"
#include <qpointer.h>

IRC_BEGIN_NAMESPACE

/*!
    \file ircusermodel.h
    \brief #include &lt;IrcUserModel&gt;
 */

/*!
    \class IrcUserModel ircusermodel.h <IrcUserModel>
    \ingroup model
    \brief Keeps track of channel users.

    In order to keep track of channel users, create an instance of IrcUserModel.
    It will notify via signals when users are added and/or removed. IrcUserModel
    can be used directly as a data model for Qt's item views - both in C++ and QML.

    \code
    void ChatView::setChannel(IrcChannel* channel)
    {
        IrcUserModel* model = new IrcUserModel(channel);
        connect(model, SIGNAL(added(IrcUser*)), this, SLOT(onUserAdded(IrcUser*)));
        connect(model, SIGNAL(removed(IrcUser*)), this, SLOT(onUserRemoved(IrcUser*)));
        nickCompleter->setModel(model);
        userListView->setModel(model);
    }
    \endcode

    \section sorting Sorting

    The order of \ref users is kept as sent from the server. Furthermore,
    an alphabetical list of \ref names is provided for convenience.

    Whilst these available options cater basic use cases, for more fine-
    grained sorting it is recommended to implement a proxy model. The
    following example illustrates how to sort users by prefix and name.

    \code
    class IrcUserProxyModel : public QSortFilterProxyModel
    {
    public:
        IrcUserProxyModel(IrcUserModel* userModel) : QSortFilterProxyModel(userModel)
        {
            setSourceModel(userModel);
        }

        // IrcNetwork::prefixes()
        void sortByPrefixes(const QStringList& prefixes)
        {
            m_prefixes = prefixes;
            sort(0, Qt::AscendingOrder);
            setDynamicSortFilter(true);
        }

    protected:
        bool lessThan(const QModelIndex& left, const QModelIndex& right) const
        {
            const QString p1 = left.data(Irc::PrefixRole).toString();
            const QString p2 = right.data(Irc::PrefixRole).toString();

            const int i1 = !p1.isEmpty() ? m_prefixes.indexOf(p1.at(0)) : -1;
            const int i2 = !p2.isEmpty() ? m_prefixes.indexOf(p2.at(0)) : -1;

            if (i1 >= 0 && i2 < 0)
                return true;
            if (i1 < 0 && i2 >= 0)
                return false;
            if (i1 >= 0 && i2 >= 0 && i1 != i2)
                return i1 < i2;

            const QString n1 = left.data(Irc::NameRole).toString();
            const QString n2 = right.data(Irc::NameRole).toString();
            return n1.compare(n2, Qt::CaseInsensitive) < 0;
        }

    private:
        QStringList m_prefixes;
    };
    \endcode

    \sa model
*/

/*!
    \fn void IrcUserModel::added(IrcUser* user)

    This signal is emitted when a \a user is added to the list of users.
 */

/*!
    \fn void IrcUserModel::removed(IrcUser* user)

    This signal is emitted when a \a user is removed from the list of users.
 */

class IrcUserLessThan
{
public:
    IrcUserLessThan(IrcUserModel* model) : model(model) { }
    bool operator()(const IrcUser* u1, const IrcUser* u2) const { return model->lessThan(u1, u2); }
private:
    IrcUserModel* model;
};

class IrcUserGreaterThan
{
public:
    IrcUserGreaterThan(IrcUserModel* model) : model(model) { }
    bool operator()(const IrcUser* u1, const IrcUser* u2) const { return model->lessThan(u2, u1); }
private:
    IrcUserModel* model;
};

IrcUserModelPrivate::IrcUserModelPrivate(IrcUserModel* q) :
    q_ptr(q), role(Irc::TitleRole), sortOrder(Qt::AscendingOrder), dynamicSort(false)
{
}

void IrcUserModelPrivate::addUser(IrcUser* user)
{
    Q_Q(IrcUserModel);
    int idx = userList.count();
    if (dynamicSort) {
        QList<IrcUser*>::iterator it;
        if (sortOrder == Qt::AscendingOrder)
            it = qUpperBound(userList.begin(), userList.end(), user, IrcUserLessThan(q));
        else
            it = qUpperBound(userList.begin(), userList.end(), user, IrcUserGreaterThan(q));
        idx = it - userList.begin();
    }
    q->beginInsertRows(QModelIndex(), idx, idx);
    userList.insert(idx, user);
    q->endInsertRows();
    emit q->added(user);
    emit q->namesChanged(IrcChannelPrivate::get(channel)->userMap.keys());
    emit q->usersChanged(userList);
    emit q->countChanged(userList.count());
}

void IrcUserModelPrivate::removeUser(IrcUser* user)
{
    Q_Q(IrcUserModel);
    int idx = userList.indexOf(user);
    if (idx != -1) {
        q->beginRemoveRows(QModelIndex(), idx, idx);
        userList.removeAt(idx);
        q->endRemoveRows();
        emit q->removed(user);
        emit q->namesChanged(IrcChannelPrivate::get(channel)->userMap.keys());
        emit q->usersChanged(userList);
        emit q->countChanged(userList.count());
    }
}

void IrcUserModelPrivate::setUsers(const QList<IrcUser*>& users)
{
    Q_Q(IrcUserModel);
    q->beginResetModel();
    userList = users;
    if (dynamicSort) {
        if (sortOrder == Qt::AscendingOrder)
            qSort(userList.begin(), userList.end(), IrcUserLessThan(q));
        else
            qSort(userList.begin(), userList.end(), IrcUserGreaterThan(q));
    }
    foreach (IrcUser* user, userList)
        emit q->added(user);
    q->endResetModel();
    emit q->namesChanged(IrcChannelPrivate::get(channel)->userMap.keys());
    emit q->usersChanged(userList);
    emit q->countChanged(userList.count());
}

/*!
    Constructs a new model with \a parent.

    \note If \a parent is an instance of IrcChannel, it will be
    automatically assigned to \ref IrcUserModel::channel "channel".
 */
IrcUserModel::IrcUserModel(QObject* parent)
    : QAbstractListModel(parent), d_ptr(new IrcUserModelPrivate(this))
{
    setChannel(qobject_cast<IrcChannel*>(parent));

    qRegisterMetaType<IrcUser*>();
    qRegisterMetaType<QList<IrcUser*> >();
}

/*!
    Destructs the model.
 */
IrcUserModel::~IrcUserModel()
{
    Q_D(IrcUserModel);
    if (d->channel)
        IrcChannelPrivate::get(d->channel)->userModels.removeOne(this);
}

/*!
    This property holds the channel.

    \par Access functions:
    \li \ref IrcChannel* <b>channel</b>() const
    \li void <b>setChannel</b>(\ref IrcChannel* channel)
 */
IrcChannel* IrcUserModel::channel() const
{
    Q_D(const IrcUserModel);
    return d->channel;
}

void IrcUserModel::setChannel(IrcChannel* channel)
{
    Q_D(IrcUserModel);
    if (d->channel != channel) {
        beginResetModel();
        if (d->channel)
            IrcChannelPrivate::get(d->channel)->userModels.removeOne(this);

        d->channel = channel;

        if (d->channel) {
            IrcChannelPrivate::get(d->channel)->userModels.append(this);
            d->setUsers(IrcChannelPrivate::get(d->channel)->userList);
        }
        endResetModel();

        emit channelChanged(channel);
    }
}

/*!
    This property holds the number of users on the channel.

    \par Access function:
    \li int <b>count</b>() const

    \par Notifier signal:
    \li void <b>countChanged</b>(int count)
 */
int IrcUserModel::count() const
{
    return rowCount();
}

/*!
    This property holds the list of names in alphabetical order.

    \par Access function:
    \li QStringList <b>names</b>() const

    \par Notifier signal:
    \li void <b>namesChanged</b>(const QStringList& names)
 */
QStringList IrcUserModel::names() const
{
    Q_D(const IrcUserModel);
    if (d->channel)
        return IrcChannelPrivate::get(d->channel)->userMap.keys();
    return QStringList();
}

/*!
    This property holds the list of users.

    The order of users is kept as sent from the server.

    \par Access function:
    \li QList<\ref IrcUser*> <b>users</b>() const

    \par Notifier signal:
    \li void <b>usersChanged</b>(const QList<\ref IrcUser*>& users)
 */
QList<IrcUser*> IrcUserModel::users() const
{
    Q_D(const IrcUserModel);
    return d->userList;
}

/*!
    Returns the user object at \a index.
 */
IrcUser* IrcUserModel::get(int index) const
{
    Q_D(const IrcUserModel);
    return d->userList.value(index);
}

/*!
    Returns the user object for \a name.
 */
IrcUser* IrcUserModel::user(const QString& name) const
{
    Q_D(const IrcUserModel);
    if (d->channel)
        return IrcChannelPrivate::get(d->channel)->userMap.value(name);
    return 0;
}

/*!
    Returns \c true if the model contains \a name.
 */
bool IrcUserModel::contains(const QString& name) const
{
    Q_D(const IrcUserModel);
    if (d->channel)
        return IrcChannelPrivate::get(d->channel)->userMap.contains(name);
    return false;
}

/*!
    Returns the index of the specified \a user,
    or \c -1 if the model does not contain the \a user.
 */
int IrcUserModel::indexOf(IrcUser* user) const
{
    Q_D(const IrcUserModel);
    return d->userList.indexOf(user);
}

/*!
    This property holds whether the model is dynamically sorted.

    The default value is \c false.

    \par Access functions:
    \li bool <b>dynamicSort</b>() const
    \li void <b>setDynamicSort</b>(bool dynamic)

    \sa sort(), lessThan()
 */
bool IrcUserModel::dynamicSort() const
{
    Q_D(const IrcUserModel);
    return d->dynamicSort;
}

void IrcUserModel::setDynamicSort(bool dynamic)
{
    Q_D(IrcUserModel);
    d->dynamicSort = dynamic;
}

/*!
    This property holds the display role.


    The specified data role is returned for Qt::DisplayRole.

    The default value is \ref Irc::TitleRole.

    \par Access functions:
    \li \ref Irc::ItemDataRole <b>displayRole</b>() const
    \li void <b>setDisplayRole</b>(\ref Irc::ItemDataRole role)
 */
Irc::ItemDataRole IrcUserModel::displayRole() const
{
    Q_D(const IrcUserModel);
    return d->role;
}

void IrcUserModel::setDisplayRole(Irc::ItemDataRole role)
{
    Q_D(IrcUserModel);
    d->role = role;
}

/*!
    Returns the model index for \a user.
 */
QModelIndex IrcUserModel::index(IrcUser* user) const
{
    Q_D(const IrcUserModel);
    return index(d->userList.indexOf(user));
}

/*!
    Returns the user for model \a index.
 */
IrcUser* IrcUserModel::user(const QModelIndex& index) const
{
    if (!hasIndex(index.row(), index.column()))
        return 0;

    return static_cast<IrcUser*>(index.internalPointer());
}

/*!
    The following role names are provided by default:

    Role            | Name      | Type     | Example
    --------------- | ----------|----------|--------
    Qt::DisplayRole | "display" | 1)       | -
    Irc::UserRole   | "user"    | IrcUser* | &lt;object&gt;
    Irc::NameRole   | "name"    | QString  | "jpnurmi"
    Irc::PrefixRole | "prefix"  | QString  | "@"
    Irc::ModeRole   | "mode"    | QString  | "o"
    Irc::TitleRole  | "title"   | QString  | "@jpnurmi"

    1) The type depends on \ref displayRole.
 */
QHash<int, QByteArray> IrcUserModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[Irc::UserRole] = "user";
    roles[Irc::NameRole] = "name";
    roles[Irc::PrefixRole] = "prefix";
    roles[Irc::ModeRole] = "mode";
    roles[Irc::TitleRole] = "title";
    return roles;
}

/*!
    Returns the number of users on the channel.
 */
int IrcUserModel::rowCount(const QModelIndex& parent) const
{
    Q_D(const IrcUserModel);
    if (parent.isValid() || !d->channel)
        return 0;

    return d->userList.count();
}

/*!
    Returns the data for specified \a role referred to by the \a index.

    \sa Irc::ItemDataRole, roleNames()
 */
QVariant IrcUserModel::data(const QModelIndex& index, int role) const
{
    Q_D(const IrcUserModel);
    if (!d->channel || !hasIndex(index.row(), index.column()))
        return QVariant();

    IrcUser* user = static_cast<IrcUser*>(index.internalPointer());

    switch (role) {
    case Qt::DisplayRole:
        return data(index, d->role);
    case Irc::UserRole:
        return QVariant::fromValue(user);
    case Irc::NameRole:
        if (user)
            return user->name();
        break;
    case Irc::PrefixRole:
        if (user)
            return user->prefix().left(1);
        break;
    case Irc::ModeRole:
        if (user)
            return user->mode().left(1);
        break;
    case Irc::TitleRole:
        if (user)
            return user->prefix().left(1) + user->name();
        break;
    }

    return QVariant();
}

/*!
    Returns the index of the item in the model specified by the given \a row, \a column and \a parent index.
 */
QModelIndex IrcUserModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_D(const IrcUserModel);
    if (!d->channel || !hasIndex(row, column, parent))
        return QModelIndex();

    return createIndex(row, column, d->userList.value(row));
}

/*!
    \reimp
    Sorts the model in the given \a order.

    \sa lessThan(), dynamicSort
 */
void IrcUserModel::sort(int column, Qt::SortOrder order)
{
    Q_UNUSED(column);
    Q_D(IrcUserModel);

    emit layoutAboutToBeChanged();

    QList<IrcUser*> persistentUsers;
    QModelIndexList oldPersistentIndexes = persistentIndexList();
    foreach (const QModelIndex& index, oldPersistentIndexes)
        persistentUsers += static_cast<IrcUser*>(index.internalPointer());

    d->sortOrder = order;
    if (order == Qt::AscendingOrder)
        qSort(d->userList.begin(), d->userList.end(), IrcUserLessThan(this));
    else
        qSort(d->userList.begin(), d->userList.end(), IrcUserGreaterThan(this));

    QModelIndexList newPersistentIndexes;
    foreach (IrcUser* user, persistentUsers)
        newPersistentIndexes += index(d->userList.indexOf(user));
    changePersistentIndexList(oldPersistentIndexes, newPersistentIndexes);

    emit layoutChanged();
}

/*!
    Returns \c true if \a one buffer is "less than" \a another,
    otherwise returns \c false.

    The default implementation sorts users alphabetically and
    special users (operators, voiced users) before normal users.
    Reimplement this function in order to alter the sort order.

    \sa sort(), dynamicSort
 */
bool IrcUserModel::lessThan(const IrcUser* one, const IrcUser* another) const
{
    // TODO: seriously? :D
    const IrcNetwork* network = one->channel()->model()->connection()->network();
    const QStringList prefixes = network->prefixes();

    const QString p1 = one->prefix();
    const QString p2 = another->prefix();

    const int i1 = !p1.isEmpty() ? prefixes.indexOf(p1.at(0)) : -1;
    const int i2 = !p2.isEmpty() ? prefixes.indexOf(p2.at(0)) : -1;

    if (i1 >= 0 && i2 < 0)
        return true;
    if (i1 < 0 && i2 >= 0)
        return false;
    if (i1 >= 0 && i2 >= 0 && i1 != i2)
        return i1 < i2;

    const QString n1 = one->name();
    const QString n2 = another->name();
    return n1.compare(n2, Qt::CaseInsensitive) < 0;
}

#include "moc_ircusermodel.cpp"

IRC_END_NAMESPACE