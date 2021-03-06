#pragma once

#include <QAbstractListModel>
#include <QColor>

#include "state.h"

class RolesModel : public QAbstractListModel
{
	Q_OBJECT

	struct Private;
	QScopedPointer<Private> d;

	State* s;
	SDK::Client* c;

public:
	RolesModel(SDK::Client* client, quint64 guildID, State* state);
	~RolesModel();

	Q_INVOKABLE FutureBase moveRoleFromTo(int from, int to);
	Q_INVOKABLE FutureBase createRole(QString name, QColor colour);
	Q_INVOKABLE QVariant everyonePermissions() const;

	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::DisplayRole) override;
	QHash<int,QByteArray> roleNames() const override;

};
