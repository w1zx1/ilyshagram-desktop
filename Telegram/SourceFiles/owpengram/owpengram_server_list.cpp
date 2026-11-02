/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/owpengram_server_list.h"

#include "base/timer.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "owpengram/owpengram_servers.h"
#include "storage/storage_account.h"
#include "ui/toast/toast.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QUrl>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <rpl/lifetime.h>

namespace Owpengram {
namespace {

const auto kListUrl =
	"https://raw.githubusercontent.com/w1zx1/ilyshagram-server-list"
	"/main/servers.json";
const auto kUserAgent = u"IlyshaGram"_q;
const auto kCacheFile = u"owpengram_managed_servers.json"_q;
constexpr auto kFirstRefreshDelay = crl::time(1000);
constexpr auto kRefreshInterval = crl::time(10 * 60 * 1000);
constexpr auto kMinRefreshGap = crl::time(60 * 1000);
constexpr auto kRequestTimeout = 15000;
constexpr auto kMaxListSize = 256 * 1024;
constexpr auto kMaxLogoSize = 512 * 1024;
constexpr auto kMaxLogoPathSize = 256;
constexpr auto kMaxServers = 50;
constexpr auto kMaxIdLength = 64;
constexpr auto kMaxNameLength = 128;
constexpr auto kMaxDescriptionLength = 512;
constexpr auto kMaxHostLength = 255;
constexpr auto kMaxKeyLength = 4096;
constexpr auto kMaxLogoUrlLength = 2048;

struct Record {
	Server server;
	// The URL the local logo file was actually fetched from, empty when there
	// is no local logo.
	QString logoUrl;
	// The URL the currently fetched list asks for. Kept equal to logoUrl while
	// the local file matches it.
	QString pendingLogoUrl;
};

struct Runtime {
	bool loaded = false;
	bool started = false;
	ServersState state = ServersState::Loading;
	ServersError error = ServersError::None;
	std::vector<Record> records;
	std::unique_ptr<QNetworkAccessManager> manager;
	std::unique_ptr<base::Timer> timer;
	QNetworkReply *reply = nullptr;
	QNetworkReply *logoReply = nullptr;
	int generation = 0;
	int logoGeneration = 0;
	crl::time lastAttempt = 0;
	std::vector<QString> logoQueue;
	bool logoFetching = false;
	// Set when the list was applied while the passcode lock was up: account
	// side effects (logout/migration) must not run behind the lock screen,
	// where logging out would delete data without the passcode.
	bool pendingSideEffects = false;
	rpl::lifetime passcodeLifetime;
};

[[nodiscard]] Runtime &State() {
	static auto runtime = Runtime();
	return runtime;
}

[[nodiscard]] rpl::event_stream<> &Changes() {
	static auto stream = rpl::event_stream<>();
	return stream;
}

[[nodiscard]] QString ListUrl() {
	const auto override = qEnvironmentVariable("TDESKTOP_SERVER_LIST_URL");
	return override.isEmpty() ? QString::fromLatin1(kListUrl) : override;
}

[[nodiscard]] QString CachePath() {
	return cWorkingDir() + u"tdata/"_q + kCacheFile;
}

[[nodiscard]] Record *FindRecord(const QString &id) {
	auto &records = State().records;
	for (auto &record : records) {
		if (record.server.id == id) {
			return &record;
		}
	}
	return nullptr;
}

[[nodiscard]] const Record *FindRecord(
		const std::vector<Record> &list,
		const QString &id) {
	for (const auto &record : list) {
		if (record.server.id == id) {
			return &record;
		}
	}
	return nullptr;
}

[[nodiscard]] bool SetState(ServersState state, ServersError error) {
	auto &runtime = State();
	if (runtime.state == state && runtime.error == error) {
		return false;
	}
	runtime.state = state;
	runtime.error = error;
	return true;
}

void Schedule(crl::time delay);
void FetchList();
void ProcessLogoQueue();

void CancelListReply() {
	auto &runtime = State();
	++runtime.generation;
	if (runtime.reply) {
		runtime.reply->abort();
		runtime.reply->deleteLater();
		runtime.reply = nullptr;
	}
}

void CancelLogoReply() {
	auto &runtime = State();
	++runtime.logoGeneration;
	if (runtime.logoReply) {
		runtime.logoReply->abort();
		runtime.logoReply->deleteLater();
		runtime.logoReply = nullptr;
	}
}

void Schedule(crl::time delay) {
	auto &runtime = State();
	if (!runtime.started || !runtime.timer) {
		return;
	}
	runtime.timer->cancel();
	runtime.timer->setCallback([] {
		FetchList();
	});
	runtime.timer->callOnce(delay);
}

[[nodiscard]] bool IsValidHost(const QString &host) {
	auto address = QHostAddress();
	if (address.setAddress(host)) {
		return true;
	}
	static const auto HostRegExp = QRegularExpression(
		u"^[A-Za-z0-9]([A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$"_q);
	return HostRegExp.match(host).hasMatch();
}

[[nodiscard]] std::optional<QString> ReadString(
		const QJsonObject &object,
		const QString &key,
		int maxLength,
		bool required,
		const QString &context,
		QString *error) {
	const auto value = object.value(key);
	if (value.isUndefined() || value.isNull()) {
		if (required) {
			*error = u"%1: missing '%2'"_q.arg(context, key);
			return std::nullopt;
		}
		return QString();
	} else if (!value.isString()) {
		*error = u"%1: '%2' is not a string"_q.arg(context, key);
		return std::nullopt;
	}
	const auto result = value.toString().trimmed();
	if (result.size() > maxLength) {
		*error = u"%1: '%2' is too long"_q.arg(context, key);
		return std::nullopt;
	} else if (required && result.isEmpty()) {
		*error = u"%1: '%2' is empty"_q.arg(context, key);
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<Record> ParseRecord(
		const QJsonObject &object,
		bool fromCache,
		int index,
		QString *error) {
	const auto context = u"servers[%1]"_q.arg(index);
	auto record = Record();
	auto &server = record.server;

	const auto id = ReadString(
		object,
		u"id"_q,
		kMaxIdLength,
		true,
		context,
		error);
	if (!id) {
		return std::nullopt;
	}
	static const auto IdRegExp = QRegularExpression(u"^[A-Za-z0-9_-]+$"_q);
	if (*id == QString::fromLatin1(kTelegramServerId)) {
		*error = u"%1: reserved id"_q.arg(context);
		return std::nullopt;
	} else if (!IdRegExp.match(*id).hasMatch()) {
		*error = u"%1: invalid id"_q.arg(context);
		return std::nullopt;
	}
	server.id = *id;

	const auto name = ReadString(
		object,
		u"name"_q,
		kMaxNameLength,
		false,
		context,
		error);
	if (!name) {
		return std::nullopt;
	}
	server.name = *name;

	const auto description = ReadString(
		object,
		u"description"_q,
		kMaxDescriptionLength,
		false,
		context,
		error);
	if (!description) {
		return std::nullopt;
	}
	server.description = *description;

	const auto host = ReadString(
		object,
		u"host"_q,
		kMaxHostLength,
		true,
		context,
		error);
	if (!host) {
		return std::nullopt;
	} else if (!IsValidHost(*host)) {
		*error = u"%1: invalid host"_q.arg(context);
		return std::nullopt;
	}
	server.host = *host;

	const auto portValue = object.value(u"port"_q);
	if (!portValue.isDouble()) {
		*error = u"%1: invalid port"_q.arg(context);
		return std::nullopt;
	}
	const auto port = portValue.toInt();
	if (port <= 0 || port > 65535) {
		*error = u"%1: invalid port"_q.arg(context);
		return std::nullopt;
	}
	server.port = port;

	server.multiDc = object.value(u"multiDc"_q).toBool(false);

	const auto mainDcValue = object.value(u"mainDcId"_q);
	if (!mainDcValue.isUndefined() && !mainDcValue.isNull()
		&& !mainDcValue.isDouble()) {
		*error = u"%1: invalid mainDcId"_q.arg(context);
		return std::nullopt;
	}
	const auto mainDcId = mainDcValue.toInt(0);
	if (mainDcId < 0 || mainDcId > 5) {
		*error = u"%1: invalid mainDcId"_q.arg(context);
		return std::nullopt;
	}
	server.mainDcId = mainDcId;

	const auto key = ReadString(
		object,
		u"rsaPublicKey"_q,
		kMaxKeyLength,
		false,
		context,
		error);
	if (!key) {
		return std::nullopt;
	}
	server.rsaPublicKey = *key;
	if (server.rsaPublicKey.isEmpty()) {
		if (!server.multiDc) {
			*error = u"%1: missing rsaPublicKey"_q.arg(context);
			return std::nullopt;
		}
	} else if (!IsValidRsaPublicKeyPem(server.rsaPublicKey)) {
		*error = u"%1: invalid rsaPublicKey"_q.arg(context);
		return std::nullopt;
	}

	const auto logo = ReadString(
		object,
		u"logo"_q,
		kMaxLogoUrlLength,
		false,
		context,
		error);
	if (!logo) {
		return std::nullopt;
	}
	if (!logo->isEmpty()) {
		const auto url = QUrl(*logo);
		if (!url.isValid()
			|| url.scheme().compare(u"https"_q, Qt::CaseInsensitive) != 0
			|| url.host().isEmpty()) {
			*error = u"%1: invalid logo url"_q.arg(context);
			return std::nullopt;
		}
		record.pendingLogoUrl = *logo;
	}

	server.isOfficial = true;
	server.managed = true;
	server.isTelegram = false;

	if (fromCache) {
		const auto path = object.value(u"logoPath"_q).toString();
		const auto valid = !path.isEmpty()
			&& path.size() <= kMaxLogoPathSize
			&& path.startsWith(u"owpengram_server_logos/@list_"_q)
			&& !path.contains(u".."_q)
			&& IsServerLogoPath(path)
			&& QFileInfo(ResolveServerLogoPath(path)).exists();
		if (valid) {
			server.logoPath = path;
			record.logoUrl = record.pendingLogoUrl;
		} else {
			record.pendingLogoUrl.clear();
		}
	}
	return record;
}

[[nodiscard]] std::optional<std::vector<Record>> ParseServers(
		const QByteArray &data,
		bool fromCache,
		QString *error) {
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(data, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		*error = parseError.errorString();
		return std::nullopt;
	} else if (!document.isObject()) {
		*error = u"root is not an object"_q;
		return std::nullopt;
	}
	const auto list = document.object().value(u"servers"_q);
	if (!list.isArray()) {
		*error = u"servers is not an array"_q;
		return std::nullopt;
	}
	const auto array = list.toArray();
	if (array.isEmpty()) {
		*error = u"servers is empty"_q;
		return std::nullopt;
	} else if (array.size() > kMaxServers) {
		*error = u"too many servers"_q;
		return std::nullopt;
	}

	auto result = std::vector<Record>();
	result.reserve(array.size());
	auto ids = QSet<QString>();
	auto endpoints = QSet<QString>();
	for (auto i = 0; i != array.size(); ++i) {
		if (!array.at(i).isObject()) {
			*error = u"servers[%1] is not an object"_q.arg(i);
			return std::nullopt;
		}
		auto record = ParseRecord(array.at(i).toObject(), fromCache, i, error);
		if (!record) {
			return std::nullopt;
		}
		const auto &server = record->server;
		if (ids.contains(server.id)) {
			*error = u"duplicate id '%1'"_q.arg(server.id);
			return std::nullopt;
		}
		ids.insert(server.id);
		const auto endpoint = server.host.toLower()
			+ u":"_q
			+ QString::number(server.port);
		if (endpoints.contains(endpoint)) {
			*error = u"duplicate endpoint '%1'"_q.arg(endpoint);
			return std::nullopt;
		}
		endpoints.insert(endpoint);
		result.push_back(std::move(*record));
	}
	return result;
}

[[nodiscard]] bool ServerFieldsEqual(const Server &a, const Server &b) {
	return a.id == b.id
		&& a.name == b.name
		&& a.host == b.host
		&& a.port == b.port
		&& a.description == b.description
		&& a.rsaPublicKey == b.rsaPublicKey
		&& a.multiDc == b.multiDc
		&& a.mainDcId == b.mainDcId;
}

[[nodiscard]] bool RecordsEqual(
		const std::vector<Record> &a,
		const std::vector<Record> &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (auto i = 0; i != a.size(); ++i) {
		if (!ServerFieldsEqual(a[i].server, b[i].server)
			|| a[i].pendingLogoUrl != b[i].pendingLogoUrl) {
			return false;
		}
	}
	return true;
}

void WriteCache() {
	const auto &records = State().records;
	auto array = QJsonArray();
	for (const auto &record : records) {
		auto object = ServerToJson(record.server);
		if (!record.logoUrl.isEmpty()) {
			object.insert(u"logo"_q, record.logoUrl);
		}
		array.push_back(object);
	}
	auto root = QJsonObject();
	root.insert(u"fetchedAt"_q, qint64(QDateTime::currentSecsSinceEpoch()));
	root.insert(u"servers"_q, array);

	const auto path = CachePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Server list: could not write '%1'").arg(path));
		return;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	if (!file.commit()) {
		LOG(("Server list: could not commit '%1'").arg(path));
	}
}

enum class LogoutReason {
	Removed,
	Changed,
};

void LogOutServerAccounts(
		const QString &serverId,
		const QString &name,
		LogoutReason reason) {
	const auto accounts = AccountsWithStoredServer(serverId);
	if (accounts.empty()) {
		return;
	}
	auto displayed = name;
	if (displayed.isEmpty()) {
		if (const auto selection = accounts.front()->local().readOwpengramServer()) {
			displayed = selection->host;
		}
	}
	if (displayed.isEmpty()) {
		displayed = serverId;
	}
	for (const auto &account : accounts) {
		account->logOut();
	}
	Ui::Toast::Show((reason == LogoutReason::Removed)
		? tr::lng_owpengram_server_removed_logout(
			tr::now,
			lt_count,
			float64(accounts.size()),
			lt_name,
			displayed)
		: tr::lng_owpengram_server_changed_logout(
			tr::now,
			lt_count,
			float64(accounts.size()),
			lt_name,
			displayed));
}

void ReapplyServerToAccounts(const QString &serverId) {
	for (const auto &account : AccountsWithStoredServer(serverId)) {
		if (account->mtpExists()) {
			RestoreServerToAccount(account);
		}
	}
}

void ApplySideEffects(
		const std::vector<Record> &previous,
		const std::vector<Record> &next) {
	auto customIds = QSet<QString>();
	for (const auto &server : ListServers()) {
		if (!server.managed && !server.isTelegram) {
			customIds.insert(server.id);
		}
	}

	auto baseline = QSet<QString>();
	for (const auto &record : previous) {
		baseline.insert(record.server.id);
	}
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (const auto selection = account->local().readOwpengramServer()) {
			if (selection->id != QString::fromLatin1(kTelegramServerId)
				&& !customIds.contains(selection->id)) {
				baseline.insert(selection->id);
			}
		}
	}
	auto nextIds = QSet<QString>();
	for (const auto &record : next) {
		nextIds.insert(record.server.id);
	}

	for (const auto &id : baseline) {
		if (nextIds.contains(id)) {
			continue;
		}
		const auto removed = FindRecord(previous, id);
		LogOutServerAccounts(
			id,
			removed ? removed->server.name : QString(),
			LogoutReason::Removed);
		if (removed && !removed->server.logoPath.isEmpty()) {
			RemoveServerLogoFile(removed->server.logoPath);
		}
	}

	for (const auto &record : next) {
		const auto old = FindRecord(previous, record.server.id);
		if (old) {
			const auto keyChanged = ServerKeyFingerprint(
				old->server.rsaPublicKey)
				!= ServerKeyFingerprint(record.server.rsaPublicKey);
			const auto configChanged
				= (old->server.mainDcId != record.server.mainDcId)
				|| (old->server.multiDc != record.server.multiDc);
			if (keyChanged || configChanged) {
				LogOutServerAccounts(
					record.server.id,
					record.server.name,
					LogoutReason::Changed);
				continue;
			}
		}
		UpdateStoredServerEndpoint(
			record.server.id,
			record.server.host,
			record.server.port);
		if (!old) {
			ReapplyServerToAccounts(record.server.id);
		}
	}
}

void HandleLogoReply(
		QNetworkReply *reply,
		const QString &serverId,
		const QString &url) {
	auto &runtime = State();
	runtime.logoFetching = false;
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (reply->error() != QNetworkReply::NoError || status != 200) {
		LOG(("Server list: logo for '%1' failed with error %2, status %3"
			).arg(serverId
			).arg(int(reply->error())
			).arg(status));
		ProcessLogoQueue();
		return;
	}
	const auto data = reply->readAll();
	if (data.size() > kMaxLogoSize) {
		LOG(("Server list: logo for '%1' is too large").arg(serverId));
		ProcessLogoQueue();
		return;
	}
	const auto record = FindRecord(serverId);
	if (record && record->pendingLogoUrl == url) {
		if (const auto saved = SaveListServerLogo(serverId, data)) {
			const auto previousLogo = record->server.logoPath;
			record->server.logoPath = *saved;
			record->logoUrl = url;
			if (previousLogo != *saved && !previousLogo.isEmpty()) {
				RemoveServerLogoFile(previousLogo);
			}
			WriteCache();
			Changes().fire({});
		} else {
			LOG(("Server list: logo for '%1' is not a valid image"
				).arg(serverId));
		}
	}
	ProcessLogoQueue();
}

void ProcessLogoQueue() {
	auto &runtime = State();
	if (runtime.logoFetching
		|| !runtime.started
		|| !runtime.manager
		|| runtime.logoQueue.empty()) {
		return;
	}
	const auto serverId = runtime.logoQueue.front();
	runtime.logoQueue.erase(runtime.logoQueue.begin());
	const auto record = FindRecord(serverId);
	if (!record || record->pendingLogoUrl.isEmpty()) {
		ProcessLogoQueue();
		return;
	}
	const auto haveLocal = !record->server.logoPath.isEmpty()
		&& QFileInfo(ResolveServerLogoPath(
			record->server.logoPath)).exists();
	if (record->pendingLogoUrl == record->logoUrl && haveLocal) {
		ProcessLogoQueue();
		return;
	}
	const auto url = record->pendingLogoUrl;
	const auto generation = ++runtime.logoGeneration;
	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setAttribute(
		QNetworkRequest::CacheLoadControlAttribute,
		QNetworkRequest::AlwaysNetwork);
	request.setTransferTimeout(kRequestTimeout);
	auto *reply = runtime.manager->get(request);
	runtime.logoReply = reply;
	runtime.logoFetching = true;
	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		reply,
		[&runtime, reply, generation](qint64 received, qint64 total) {
			if (generation != runtime.logoGeneration) {
				return;
			} else if (total > kMaxLogoSize || received > kMaxLogoSize) {
				reply->abort();
			}
		});
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		reply,
		[&runtime, reply, generation, serverId, url] {
			if (generation != runtime.logoGeneration) {
				return;
			}
			runtime.logoReply = nullptr;
			reply->deleteLater();
			HandleLogoReply(reply, serverId, url);
		});
}

void QueueLogoDownloads() {
	auto &runtime = State();
	runtime.logoQueue.clear();
	auto cleared = false;
	for (auto &record : runtime.records) {
		if (record.pendingLogoUrl.isEmpty()) {
			if (!record.server.logoPath.isEmpty()) {
				RemoveServerLogoFile(record.server.logoPath);
				record.server.logoPath.clear();
				record.logoUrl.clear();
				cleared = true;
			}
		} else {
			const auto haveLocal = !record.server.logoPath.isEmpty()
				&& QFileInfo(ResolveServerLogoPath(
					record.server.logoPath)).exists();
			if (record.pendingLogoUrl != record.logoUrl || !haveLocal) {
				runtime.logoQueue.push_back(record.server.id);
			}
		}
	}
	if (cleared) {
		WriteCache();
		Changes().fire({});
	}
	ProcessLogoQueue();
}

void Apply(std::vector<Record> next) {
	auto &runtime = State();
	auto previous = std::move(runtime.records);

	for (auto &record : next) {
		if (const auto old = FindRecord(previous, record.server.id)) {
			record.logoUrl = old->logoUrl;
			record.server.logoPath = old->server.logoPath;
		}
	}

	const auto equal = RecordsEqual(previous, next);
	runtime.records = std::move(next);
	if (!equal || runtime.pendingSideEffects) {
		if (Core::App().passcodeLocked()) {
			runtime.pendingSideEffects = true;
		} else {
			runtime.pendingSideEffects = false;
			ApplySideEffects(previous, runtime.records);
		}
		if (!equal) {
			WriteCache();
		}
	}
	const auto stateChanged = SetState(ServersState::Ready, ServersError::None);
	if (!equal || stateChanged) {
		Changes().fire({});
	}
	QueueLogoDownloads();
}

void HandleFailure(ServersError kind) {
	auto &runtime = State();
	if (runtime.records.empty()
		&& SetState(ServersState::Failed, kind)) {
		Changes().fire({});
	}
	Schedule(kRefreshInterval);
}

void HandleListReply(QNetworkReply *reply) {
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const auto networkError = reply->error();
	if (networkError != QNetworkReply::NoError || status != 200) {
		const auto kind = (status == 0)
			? ServersError::Network
			: ServersError::InvalidContent;
		LOG(("Server list: request failed with error %1, status %2"
			).arg(int(networkError)
			).arg(status));
		HandleFailure(kind);
		return;
	}
	const auto data = reply->readAll();
	if (data.size() > kMaxListSize) {
		LOG(("Server list: response is too large"));
		HandleFailure(ServersError::InvalidContent);
		return;
	}
	auto error = QString();
	auto parsed = ParseServers(data, false, &error);
	if (!parsed) {
		LOG(("Server list: rejected: %1").arg(error));
		HandleFailure(ServersError::InvalidContent);
		return;
	}
	Apply(std::move(*parsed));
	Schedule(kRefreshInterval);
}

void FetchList() {
	auto &runtime = State();
	if (!runtime.started || !runtime.manager) {
		return;
	}
	CancelListReply();
	runtime.lastAttempt = crl::now();
	if (runtime.records.empty()
		&& SetState(ServersState::Loading, ServersError::None)) {
		Changes().fire({});
	}
	const auto generation = ++runtime.generation;
	auto request = QNetworkRequest(QUrl(ListUrl()));
	request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setAttribute(
		QNetworkRequest::CacheLoadControlAttribute,
		QNetworkRequest::AlwaysNetwork);
	request.setTransferTimeout(kRequestTimeout);
	auto *reply = runtime.manager->get(request);
	runtime.reply = reply;
	QObject::connect(
		reply,
		&QNetworkReply::downloadProgress,
		reply,
		[&runtime, reply, generation](qint64 received, qint64 total) {
			if (generation != runtime.generation) {
				return;
			} else if (total > kMaxListSize || received > kMaxListSize) {
				reply->abort();
			}
		});
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		reply,
		[&runtime, reply, generation] {
			if (generation != runtime.generation) {
				return;
			}
			runtime.reply = nullptr;
			reply->deleteLater();
			HandleListReply(reply);
		});
}

void EnsureLoaded() {
	auto &runtime = State();
	if (runtime.loaded) {
		return;
	}
	runtime.loaded = true;
	auto file = QFile(CachePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto data = file.readAll();
	file.close();
	if (data.isEmpty() || data.size() > kMaxListSize) {
		return;
	}
	auto error = QString();
	auto parsed = ParseServers(data, true, &error);
	if (!parsed) {
		LOG(("Server list: cache ignored: %1").arg(error));
		return;
	}
	runtime.records = std::move(*parsed);
	if (!runtime.records.empty()) {
		runtime.state = ServersState::Ready;
		runtime.error = ServersError::None;
	}
}

} // namespace

void StartManagedServers() {
	EnsureLoaded();
	auto &runtime = State();
	if (runtime.started) {
		return;
	}
	runtime.started = true;
	runtime.manager = std::make_unique<QNetworkAccessManager>();
	runtime.timer = std::make_unique<base::Timer>();
	if (Core::App().passcodeLocked()) {
		runtime.pendingSideEffects = true;
	} else if (!runtime.records.empty()) {
		// The cached list is authoritative for accounts that were never
		// reconciled (a crash between applying a removal and logging the
		// account out, or a list applied while the passcode lock was up).
		ApplySideEffects(runtime.records, runtime.records);
	}
	Core::App().passcodeLockChanges(
	) | rpl::filter([](bool locked) {
		return !locked;
	}) | rpl::on_next([] {
		RequestManagedServersRefresh(true);
	}, runtime.passcodeLifetime);
	Schedule(kFirstRefreshDelay);
}

void StopManagedServers() {
	auto &runtime = State();
	runtime.started = false;
	runtime.passcodeLifetime = rpl::lifetime();
	if (runtime.timer) {
		runtime.timer->cancel();
		runtime.timer.reset();
	}
	CancelListReply();
	CancelLogoReply();
	runtime.logoQueue.clear();
	runtime.logoFetching = false;
	runtime.manager.reset();
}

void RequestManagedServersRefresh(bool force) {
	auto &runtime = State();
	if (!runtime.started || runtime.reply) {
		return;
	}
	const auto now = crl::now();
	if (!force
		&& runtime.lastAttempt
		&& (now - runtime.lastAttempt) < kMinRefreshGap) {
		return;
	}
	if (runtime.timer) {
		runtime.timer->cancel();
	}
	FetchList();
}

ServersState CurrentServersState() {
	EnsureLoaded();
	return State().state;
}

ServersError CurrentServersError() {
	EnsureLoaded();
	return State().error;
}

rpl::producer<> ManagedServersChanges() {
	return Changes().events();
}

std::vector<Server> ManagedServers() {
	EnsureLoaded();
	const auto &records = State().records;
	auto result = std::vector<Server>();
	result.reserve(records.size());
	for (const auto &record : records) {
		result.push_back(WithServerIdentity(record.server));
	}
	return result;
}

bool IsManagedServerId(const QString &id) {
	EnsureLoaded();
	if (id.isEmpty()) {
		return false;
	}
	const auto &records = State().records;
	for (const auto &record : records) {
		if (record.server.id == id) {
			return true;
		}
	}
	return false;
}

} // namespace Owpengram
