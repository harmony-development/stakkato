// SPDX-FileCopyrightText: 2020 Carson Black <uhhadd@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QSettings>
#include <QJSEngine>
#include <QQuickTextDocument>
#include <QtConcurrent>

#include "messages.hpp"
#include "richtexter.hpp"
#include "state.hpp"
#include "channels.hpp"
#include "userroles.hpp"
#include "util.hpp"
#include "logging.hpp"

#define theHeaders {{"Authorization", client->userToken}}

State* State::s_instance;

State::State()
{
	s_instance = this;
	client = Client::mainInstance();
	guildModel = new GuildModel;
}
State::~State()
{
	delete client;
	delete guildModel;
}
State* State::instance()
{
	return s_instance;
}
void State::logOut()
{
	delete Client::mainClient;
	auto copy = Client::mainClient;
	copy->stopEvents();
	Client::mainClient = nullptr;
	client = nullptr;

	delete guildModel;

	for (auto cli : Client::clients) {
		if (cli != copy) {
			cli->stopEvents();
			delete cli;
		}
	}
	Client::clients.clear();

	for (auto channelsModel : ChannelsModel::instances) {
		delete channelsModel;
	}
	ChannelsModel::instances.clear();

	this->guildModel = new GuildModel;
	this->client = Client::mainInstance();

	guildModelChanged();

	Q_EMIT loggedOut();
}
UserRolesModel* State::userRoles(const QString &userID, const QString &guildID, const QString &homeserver)
{
	return new UserRolesModel(userID.toULongLong(), guildID.toULongLong(), homeserver, nullptr);
}
QString State::ownAvatar() const
{
	return m_ownAvatar;
}
QString State::ownUsername() const
{
	return m_ownUsername;
}
void State::fetchOwnProfile()
{
	QtConcurrent::run([this] {
		protocol::chat::v1::GetUserRequest req;
		req.set_user_id(client->userID);
		auto result = client->chatKit->GetUser(req, theHeaders);
		if (!resultOk(result)) {
			return;
		}
		auto v = unwrap(result);
		runOnMainThread("fetch own profile", [v, this] {
			m_ownAvatar = QString::fromStdString(v.user_avatar());
			m_ownUsername = QString::fromStdString(v.user_name());
			Q_EMIT ownAvatarChanged();
			Q_EMIT ownUsernameChanged();
		});
	});
}
void State::setProfile(QJsonObject obj, QJSValue then)
{
	QtConcurrent::run([this, obj, then] {
		protocol::chat::v1::ProfileUpdateRequest req;
		if (obj.contains("username")) {
			req.set_update_username(true);
			req.set_new_username(obj["username"].toString().toStdString());
		}
		if (obj.contains("avatar")) {
			req.set_update_avatar(true);
			req.set_new_avatar(obj["avatar"].toString().toStdString());
		}

		auto result = client->chatKit->ProfileUpdate(req, theHeaders);
		if (!resultOk(result)) {
			return;
		}
		auto v = unwrap(result);
	});
}
void State::startupLogin(QJSValue then)
{
	State::instance()->guildModel->beginResetModel();

	QtConcurrent::run([then, this] {
		QSettings settings;
		QVariant token = settings.value("state/token");
		QVariant hs = settings.value("state/homeserver");
		QVariant userID = settings.value("state/userid");
		auto ok = false;
		if (token.isValid() && hs.isValid() && userID.isValid()) {
			if (client->consumeToken(token.toString(), userID.value<quint64>(), hs.toString())) {
				ok = true;
			}
		}

		runOnMainThread("startupLogin completed", [then, ok] {
			State::instance()->guildModel->endResetModel();
			const_cast<QJSValue&>(then).call({ok});
		});
	});
}
ChannelsModel* State::channelsModel(const QString& guildID, const QString& homeserver)
{
	return guildModel->channelsModel(guildID.toULongLong(), homeserver);
}
MessagesModel* State::messagesModel(const QString& guildID, const QString& channelID, const QString& homeserver)
{
	return channelsModel(guildID, homeserver)->messagesModel(channelID.toULongLong());
}
bool State::createGuild(const QString &name)
{
	return client->createGuild(name);
}
bool State::joinGuild(const QString &inviteLink)
{
	auto str = inviteLink;
	str.remove(0, 10);
	auto split = str.split("/");
	if (split.length() != 2) {
		return false;
	}
	auto homeserver = split[0];
	auto invite = split[1];

	auto client = Client::instanceForHomeserver(homeserver);
	return client->joinInvite(invite);
}
bool State::leaveGuild(const QString &homeserver, const QString &id, bool isOwner)
{
	auto actualID = id.toULongLong();

	return Client::instanceForHomeserver(homeserver)->leaveGuild(actualID, isOwner);
}

void State::customEvent(QEvent *event)
{
	switch (event->type()) {
	case PleaseCallEvent::typeID: {
		auto ev = reinterpret_cast<PleaseCallEvent*>(event);

		QList<QJSValue> data;

		for (auto arg : ev->data.args) {
			data << ev->data.func.engine()->toScriptValue(arg);
		}

		ev->data.func.call(data);
		break;
	}
	case ExecuteEvent::typeID: {
		auto ev = reinterpret_cast<ExecuteEvent*>(event);
		qCDebug(MAIN_THREAD_TASKS) << "Executing main thread task" << ev->data.first;
		ev->data.second();
		break;
	}
	}
}

void State::bindTextDocument(QQuickTextDocument* doc, QObject* field)
{
	new TextFormatter(doc->textDocument(), field);
}

void callJS(QJSValue func, QList<QVariant> args)
{
	auto call = PleaseCall{func, args};
	QCoreApplication::postEvent(State::instance(), new PleaseCallEvent(call));
}
