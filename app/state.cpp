#include <QSettings>
#include <QQmlEngine>

#include "state_p.h"

State::State(QQmlEngine* object) : QObject(object), d(new Private)
{
	d->eng = object;
	createComponents();

	connect(d->sdk.get(), &SDK::ClientManager::authEvent, this, &State::handleStep);
	connect(d->sdk.get(), &SDK::ClientManager::ready, this, &State::endLogin);
	connect(d->sdk.get(), &SDK::ClientManager::ready, this, [this](const QString& hs, quint64 userID, const QString& tok) {
		QSettings settings;

		d->homeserver = hs;

		settings.setValue("state/homeserver", hs);
		settings.setValue("state/userid", userID);
		settings.setValue("state/token", tok);
	});
	connect(this, &State::endLogin, this, [this]() {
		createModels();
	});
}

State::~State() {}

void State::doInitialLogin()
{
	QSettings settings;

	QVariant token = settings.value("state/token");
	QVariant hs = settings.value("state/homeserver");
	QVariant userID = settings.value("state/userid");

	if (token.isValid() && hs.isValid() && userID.isValid()) {
		d->sdk->checkLogin(token.toString(), hs.toString(), userID.toULongLong()).then([this](bool ok) {
			if (!ok) Q_EMIT beginHomeserver();
		});
	} else {
		Q_EMIT beginHomeserver();
	}
}

void State::doLogin(const QString& homeserver)
{
	auto it = QUrl::fromUserInput(homeserver);
	d->sdk->beginAuthentication(homeserver);
	Q_EMIT beginLogin();
}

SDK::ClientManager* State::api()
{
	return d->sdk.get();
}

GuildList* State::guildList()
{
	return d->list;
}

GuildsStore* State::guildsStore()
{
	return d->store;
}

MembersStore* State::membersStore()
{
	return d->membersStore;
}

void State::createModels()
{
	d->list = new GuildList(this);
	Q_EMIT guildListChanged();

	d->store = new GuildsStore(this);
	Q_EMIT guildsStoreChanged();

	d->membersStore = new MembersStore(this);
}

Future<ChannelsModel*> State::channelsModelFor(QString host, QString guildID, QObject *it)
{
	auto eng = qmlEngine(it);

	auto r = co_await d->sdk->clientForHomeserver(host);
	if (!r.ok()) {
		co_return nullptr;
	}
	auto c = r.value();
	auto id = guildID.toULongLong();
	auto tup = qMakePair(c, id);

	if (d->channelsModels.contains(tup) and not d->channelsModels[tup].isNull()) {
		co_return d->channelsModels[tup];
	}

	auto mod = new ChannelsModel(c, id, this);
	eng->setObjectOwnership(mod, QQmlEngine::JavaScriptOwnership);
	d->channelsModels[tup] = mod;

	co_return mod;
}

Future<InviteModel*> State::inviteModelFor(QString host, QString guildID, QObject *it)
{
	auto eng = qmlEngine(it);

	auto r = co_await d->sdk->clientForHomeserver(host);
	if (!r.ok()) {
		co_return nullptr;
	}
	auto c = r.value();
	auto id = guildID.toULongLong();

	auto mod = new InviteModel(c, id, this);
	eng->setObjectOwnership(mod, QQmlEngine::JavaScriptOwnership);

	co_return mod;
}

Future<RolesModel*> State::rolesModelFor(QString host, QString guildID, QObject* it)
{
	auto eng = qmlEngine(it);

	auto r = co_await d->sdk->clientForHomeserver(host);
	if (!r.ok()) {
		co_return nullptr;
	}
	auto c = r.value();
	auto id = guildID.toULongLong();

	auto mod = new RolesModel(c, id, this);
	eng->setObjectOwnership(mod, QQmlEngine::JavaScriptOwnership);

	co_return mod;
}

Future<MembersModel*> State::membersModelFor(QString host, QString guildID, QObject *it)
{
	auto eng = qmlEngine(it);

	auto r = co_await d->sdk->clientForHomeserver(host);
	if (!r.ok()) {
		co_return nullptr;
	}
	auto c = r.value();
	auto id = guildID.toULongLong();
	auto tup = qMakePair(c, id);

	if (d->membersModels.contains(tup) and not d->membersModels[tup].isNull()) {
		co_return d->membersModels[tup];
	}

	auto mod = new MembersModel(c, id, this);
	eng->setObjectOwnership(mod, QQmlEngine::JavaScriptOwnership);
	d->membersModels[tup] = mod;

	co_return mod;
}

Future<MessagesModel*> State::messagesModelFor(QString host, QString guildID, QString channelID, QObject* it)
{
	auto eng = qmlEngine(it);

	auto r = co_await d->sdk->clientForHomeserver(host);
	if (!r.ok()) {
		co_return nullptr;
	}
	auto c = r.value();
	auto gid = guildID.toULongLong();
	auto cid = channelID.toULongLong();
	auto tup = qMakePair(c, qMakePair(gid, cid));

	if (d->messagesModels.contains(tup) and not d->messagesModels[tup].isNull()) {
		co_return d->messagesModels[tup];
	}

	auto mod = new MessagesModel(c, gid, cid, this);
	eng->setObjectOwnership(mod, QQmlEngine::JavaScriptOwnership);
	d->messagesModels[tup] = mod;

	co_return mod;
}

QString State::mediaURL(const QString& url, const QString& homeserver) {
	auto hs = homeserver;
	if (hs.isEmpty()) {
		hs = d->homeserver;
	}

	QString port;
	if (homeserver.contains(":")) {
		port = homeserver.split(":")[1];
	} else {
		port = "2289";
	}

	if (!url.startsWith("hmc://")) {
		return QString("https://%1:%2/_harmony/media/download/%3").arg(hs).arg(port).arg(url);
	}

	QString trimmed = url.mid(QString("hmc://").length());
	auto split = trimmed.split("/");
	if (split.length() != 2) {
		qWarning() << "Malformed HMC URL:" << url;
		return QString("");
	}
	return QString("https://%1/_harmony/media/download/%2").arg(split[0]).arg(split[1]);
}
