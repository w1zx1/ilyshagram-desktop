#include "discord/discord_presence.h"

#include "core/branding.h"
#include "core/version.h"

#include <QString>

#include <QCoreApplication>
#include <QDateTime>
#include <QtNetwork/QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>

namespace DiscordRpc {
namespace {

constexpr auto kDiscordClientId = "1541803105853636658";
constexpr auto kDiscordAssetKey = "icon";
constexpr auto kDiscordButtonLabel = "Source code";
constexpr auto kDiscordButtonUrl
	= "https://github.com/w1zx1/ilyshagram-desktop";

bool Running = false;
QLocalSocket *Socket = nullptr;
QTimer *ReconnectTimer = nullptr;
QStringList Candidates;
int CandidateIndex = 0;
QString CurrentClientId;
QByteArray PendingActivity;
QByteArray Buffer;

void Reconnect();

QStringList BuildCandidates() {
	QStringList result;
#ifdef Q_OS_WIN
	for (int i = 0; i != 10; ++i) {
		result << u"\\\\.\\pipe\\discord-ipc-"_q + QString::number(i);
	}
#else
	const auto xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
	const auto tmp = qEnvironmentVariable("TMPDIR");
	const auto tmpBase = tmp.endsWith('/')
		? tmp.mid(0, tmp.size() - 1)
		: tmp;
	const auto config = QStandardPaths::writableLocation(
		QStandardPaths::ConfigLocation);
	for (int i = 0; i != 10; ++i) {
		if (!xdg.isEmpty()) {
			result << (xdg + u"/discord-ipc-"_q + QString::number(i));
		}
		if (!tmpBase.isEmpty()) {
			result << (tmpBase + u"/discord-ipc-"_q + QString::number(i));
		}
		result << (config
			+ u"/discord/discord-ipc-"_q
			+ QString::number(i));
	}
#endif
	return result;
}

void SendFrame(quint32 opcode, const QByteArray &data) {
	if (!Socket
		|| Socket->state() != QLocalSocket::ConnectedState) {
		return;
	}
	QByteArray header;
	{
		QDataStream ds(&header, QIODevice::WriteOnly);
		ds.setByteOrder(QDataStream::LittleEndian);
		ds << opcode << quint32(data.size());
	}
	Socket->write(header);
	Socket->write(data);
}

void SendHandshake() {
	QJsonObject obj;
	obj["v"] = 1;
	obj["client_id"] = CurrentClientId;
	SendFrame(0, QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void SendActivity() {
	if (PendingActivity.isEmpty()) {
		return;
	}
	SendFrame(1, PendingActivity);
}

void TryConnect() {
	if (CandidateIndex >= Candidates.size()) {
		Reconnect();
		return;
	}
	Socket->connectToServer(Candidates[CandidateIndex]);
}

void Reconnect() {
	if (Socket && Socket->state() != QLocalSocket::UnconnectedState) {
		Socket->abort();
	}
	CandidateIndex = 0;
	if (!ReconnectTimer) {
		ReconnectTimer = new QTimer(QCoreApplication::instance());
		ReconnectTimer->setSingleShot(true);
		QObject::connect(ReconnectTimer, &QTimer::timeout, [] {
			TryConnect();
		});
	}
	ReconnectTimer->start(2000);
}

void OnConnected() {
	SendHandshake();
	SendActivity();
}

void OnDisconnected() {
	Reconnect();
}

void OnError() {
	if (Socket->state() == QLocalSocket::ConnectedState) {
		return;
	}
	++CandidateIndex;
	QTimer::singleShot(0, [] { TryConnect(); });
}

void OnReadyRead() {
	Buffer.append(Socket->readAll());
	while (Buffer.size() >= 8) {
		quint32 opcode = 0;
		quint32 length = 0;
		{
			QDataStream ds(Buffer);
			ds.setByteOrder(QDataStream::LittleEndian);
			ds >> opcode >> length;
		}
		if (static_cast<quint64>(Buffer.size()) < 8ull + length) {
			break;
		}
		const auto payload = Buffer.mid(8, length);
		Buffer.remove(0, 8 + length);
		if (opcode == 2) {
			Reconnect();
			return;
		} else if (opcode == 1) {
			SendActivity();
		} else if (opcode == 3) {
			SendFrame(4, payload);
		}
	}
}

} // namespace

void Start(const QString &clientId) {
	if (Running) {
		return;
	}
	Running = true;
	CurrentClientId = clientId;
	Candidates = BuildCandidates();
	CandidateIndex = 0;
	Buffer.clear();

	if (!Socket) {
		Socket = new QLocalSocket(QCoreApplication::instance());
		QObject::connect(Socket, &QLocalSocket::connected, [] {
			OnConnected();
		});
		QObject::connect(Socket, &QLocalSocket::disconnected, [] {
			OnDisconnected();
		});
		QObject::connect(Socket, &QLocalSocket::readyRead, [] {
			OnReadyRead();
		});
		QObject::connect(Socket, &QLocalSocket::errorOccurred, [] {
			OnError();
		});
	}
	TryConnect();
}

void StartDefault() {
	if (Running) {
		return;
	}
	Start(kDiscordClientId);
	UpdateDefaultPresence();
}

void Stop() {
	if (!Running) {
		return;
	}
	Running = false;
	if (ReconnectTimer) {
		ReconnectTimer->stop();
	}
	if (Socket) {
		Socket->disconnectFromServer();
		Socket->deleteLater();
		Socket = nullptr;
	}
	Buffer.clear();
}

void UpdateDefaultPresence() {
	const auto details = u"Using "_q + QString(Branding::ShortAppName.utf16());
	const auto state = u"v%1 (Telegram %2)"_q
		.arg(QString::fromLatin1(IlyshaVersionStr))
		.arg(QString::fromLatin1(AppVersionStr));
	UpdatePresence(
		details,
		state,
		QString::fromUtf8(kDiscordAssetKey),
		u"ilyshaGram"_q,
		QString::fromUtf8(kDiscordButtonLabel),
		QString::fromUtf8(kDiscordButtonUrl));
}

void UpdatePresence(
		const QString &details,
		const QString &state,
		const QString &largeImageKey,
		const QString &largeImageText,
		const QString &buttonLabel,
		const QString &buttonUrl) {
	if (!Running) {
		return;
	}
	QJsonObject activity;
	activity["details"] = details;
	activity["state"] = state;
	QJsonObject assets;
	assets["large_image"] = largeImageKey;
	assets["large_text"] = largeImageText;
	activity["assets"] = assets;
	if (!buttonLabel.isEmpty() && !buttonUrl.isEmpty()) {
		QJsonObject button;
		button["label"] = buttonLabel;
		button["url"] = buttonUrl;
		QJsonArray buttons;
		buttons.append(button);
		activity["buttons"] = buttons;
	}

	QJsonObject args;
	args["pid"] = QCoreApplication::applicationPid();
	args["activity"] = activity;

	QJsonObject command;
	command["cmd"] = "SET_ACTIVITY";
	command["args"] = args;
	command["nonce"] = QString::number(QDateTime::currentMSecsSinceEpoch());

	PendingActivity = QJsonDocument(command).toJson(QJsonDocument::Compact);
	SendActivity();
}

void ClearPresence() {
	if (!Running) {
		return;
	}
	QJsonObject args;
	args["pid"] = QCoreApplication::applicationPid();
	args["activity"] = QJsonObject();

	QJsonObject command;
	command["cmd"] = "SET_ACTIVITY";
	command["args"] = args;
	command["nonce"] = QString::number(QDateTime::currentMSecsSinceEpoch());

	PendingActivity = QJsonDocument(command).toJson(QJsonDocument::Compact);
	SendActivity();
}

void Pump() {
}

bool IsRunning() {
	return Running;
}

} // namespace DiscordRpc
