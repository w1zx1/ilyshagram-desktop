/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/github_updates.h"

#include "core/application.h"
#include "core/version.h"
#include "settings.h"
#include "base/timer.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

#ifdef Q_OS_WIN
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#endif // Q_OS_WIN

namespace GithubUpdates {
namespace {

constexpr auto kFirstCheckDelay = crl::time(60 * 1000);
constexpr auto kCheckInterval = crl::time(3600 * 1000);
constexpr auto kRateLimitBackoff = crl::time(6 * 3600 * 1000);
constexpr auto kMaxImmediateRetries = 1;

constexpr auto kPendingDir = "tdata/pending_update/";
constexpr auto kPendingExe = "ilyshagram-setup.exe";
constexpr auto kPendingVersionFile = "version.txt";

State CurrentState = State::Idle;
rpl::event_stream<State> StateEvents;

void SetState(State state) {
	if (CurrentState == state) {
		return;
	}
	CurrentState = state;
	StateEvents.fire_copy(state);
}

[[nodiscard]] QString PendingDir() {
	return cWorkingDir() + kPendingDir;
}

[[nodiscard]] QString PendingExePath() {
	return PendingDir() + kPendingExe;
}

[[nodiscard]] QString PendingVersionPath() {
	return PendingDir() + kPendingVersionFile;
}

[[nodiscard]] std::vector<int> ParseDots(const QString &version) {
	auto result = std::vector<int>();
	for (const auto &part : version.split('.')) {
		auto ok = false;
		const auto value = part.toInt(&ok);
		result.push_back(ok ? value : 0);
	}
	return result;
}

[[nodiscard]] bool IsTagNewer(const QString &tag, const QString &current) {
	auto cleaned = tag.trimmed();
	if (cleaned.startsWith('v') || cleaned.startsWith('V')) {
		cleaned = cleaned.mid(1);
	}
	if (cleaned.isEmpty() || current.isEmpty()) {
		return false;
	}
	const auto left = ParseDots(cleaned);
	const auto right = ParseDots(current);
	const auto count = std::max(left.size(), right.size());
	for (auto i = size_t(0); i != count; ++i) {
		const auto l = (i < left.size()) ? left[i] : 0;
		const auto r = (i < right.size()) ? right[i] : 0;
		if (l != r) {
			return l > r;
		}
	}
	return false;
}

[[nodiscard]] QString CurrentVersion() {
	return QString::fromLatin1(IlyshaVersionStr);
}

struct PendingInfo {
	QString tag;
	qint64 size = 0;
	bool valid = false;
};

[[nodiscard]] PendingInfo ReadPending() {
	auto result = PendingInfo();
	QFile versionFile(PendingVersionPath());
	if (!versionFile.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto lines = QString::fromUtf8(versionFile.readAll()).split('\n');
	if (lines.size() < 2) {
		return result;
	}
	auto ok = false;
	const auto size = lines[1].trimmed().toLongLong(&ok);
	if (!ok || size <= 0) {
		return result;
	}
	QFile exe(PendingExePath());
	if (!exe.exists() || exe.size() != size) {
		return result;
	}
	result.tag = lines[0].trimmed();
	result.size = size;
	result.valid = !result.tag.isEmpty();
	return result;
}

void WritePending(const QString &tag, qint64 size) {
	QDir().mkpath(PendingDir());
	QFile versionFile(PendingVersionPath());
	if (versionFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		versionFile.write(tag.toUtf8() + '\n' + QByteArray::number(size));
	}
}

void ClearPending() {
	QFile::remove(PendingExePath());
	QFile::remove(PendingVersionPath());
}

#ifdef Q_OS_WIN
constexpr auto kApiUrl = "https://api.github.com/w1zx1/ilyshagram-desktop/releases/latest";
constexpr auto kAssetName = "ilyshagram-setup.exe";
constexpr auto kUserAgent = "ilyshaGram";
constexpr auto kInstallDelaySeconds = 5;

base::Timer CheckTimer;
std::unique_ptr<QNetworkAccessManager> Manager;
QNetworkReply *ActiveReply = nullptr;
std::unique_ptr<QFile> ActiveFile;
QString PendingTag;
qint64 PendingSize = 0;
int ImmediateRetries = 0;
int ReplyGeneration = 0;
bool Started = false;

void Check();

void CancelActiveReply() {
	++ReplyGeneration;
	if (ActiveReply) {
		ActiveReply->abort();
		ActiveReply->deleteLater();
		ActiveReply = nullptr;
	}
	ActiveFile.reset();
}

void ScheduleNext(crl::time delay) {
	CheckTimer.cancel();
	CheckTimer.setCallback([] { Check(); });
	CheckTimer.callOnce(delay);
}

void FinishDownload(bool ok) {
	ActiveReply = nullptr;
	if (ActiveFile) {
		ActiveFile->close();
	}
	if (ok && ActiveFile && QFile::exists(PendingExePath())
		&& QFile(PendingExePath()).size() == PendingSize) {
		WritePending(PendingTag, PendingSize);
		ActiveFile.reset();
		SetState(State::Ready);
		ScheduleNext(kCheckInterval);
		return;
	}
	ActiveFile.reset();
	QFile::remove(PendingExePath());
	if (ImmediateRetries < kMaxImmediateRetries) {
		++ImmediateRetries;
		Check();
		return;
	}
	SetState(State::Failed);
	ScheduleNext(kCheckInterval);
}

void StartDownload(const QString &url) {
	CancelActiveReply();
	ImmediateRetries = 0;
	QDir().mkpath(PendingDir());
	QFile::remove(PendingExePath());
	ActiveFile = std::make_unique<QFile>(PendingExePath());
	if (!ActiveFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		FinishDownload(false);
		return;
	}
	auto request = QNetworkRequest(QUrl(url));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
	const auto generation = ++ReplyGeneration;
	ActiveReply = Manager->get(request);
	QObject::connect(ActiveReply, &QNetworkReply::readyRead, [=] {
		if (generation != ReplyGeneration || !ActiveFile) {
			return;
		}
		ActiveFile->write(ActiveReply->readAll());
	});
	QObject::connect(ActiveReply, &QNetworkReply::finished, [=] {
		if (generation != ReplyGeneration) {
			return;
		}
		if (ActiveFile) {
			ActiveFile->write(ActiveReply->readAll());
		}
		const auto error = ActiveReply->error();
		ActiveReply->deleteLater();
		FinishDownload(error == QNetworkReply::NoError);
	});
	SetState(State::Downloading);
}

void HandleReleaseReply(QNetworkReply *reply) {
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (reply->error() != QNetworkReply::NoError) {
		SetState(State::Failed);
		ScheduleNext(status == 403 ? kRateLimitBackoff : kCheckInterval);
		return;
	}
	const auto document = QJsonDocument::fromJson(reply->readAll());
	if (!document.isObject()) {
		SetState(State::Failed);
		ScheduleNext(kCheckInterval);
		return;
	}
	const auto object = document.object();
	const auto tag = object.value(u"tag_name"_q).toString();
	if (!IsTagNewer(tag, CurrentVersion())) {
		SetState(State::Idle);
		ScheduleNext(kCheckInterval);
		return;
	}
	auto url = QString();
	auto size = qint64(0);
	for (const auto &value : object.value(u"assets"_q).toArray()) {
		const auto asset = value.toObject();
		if (asset.value(u"name"_q).toString() == kAssetName) {
			url = asset.value(u"browser_download_url"_q).toString();
			size = qint64(asset.value(u"size"_q).toDouble());
			break;
		}
	}
	if (url.isEmpty() || size <= 0) {
		SetState(State::Failed);
		ScheduleNext(kCheckInterval);
		return;
	}
	const auto pending = ReadPending();
	if (pending.valid && pending.tag == tag && pending.size == size) {
		SetState(State::Ready);
		ScheduleNext(kCheckInterval);
		return;
	}
	PendingTag = tag;
	PendingSize = size;
	StartDownload(url);
}

void Check() {
	if (!Manager) {
		return;
	}
	CancelActiveReply();
	auto request = QNetworkRequest(QUrl(kApiUrl));
	request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
	request.setRawHeader("Accept", "application/vnd.github+json");
	const auto generation = ++ReplyGeneration;
	ActiveReply = Manager->get(request);
	QObject::connect(ActiveReply, &QNetworkReply::finished, [=] {
		if (generation != ReplyGeneration) {
			return;
		}
		const auto reply = ActiveReply;
		ActiveReply = nullptr;
		reply->deleteLater();
		HandleReleaseReply(reply);
	});
	SetState(State::Checking);
}

void LaunchInstallerDelayed(const QString &exePath) {
	const auto native = QDir::toNativeSeparators(exePath);
	const auto command = u"timeout /t %1 /nobreak >nul & \"%2\" /VERYSILENT"_q
		.arg(kInstallDelaySeconds)
		.arg(native);
	QProcess::startDetached(u"cmd.exe"_q, { u"/c"_q, command });
}

[[nodiscard]] bool ConsumeValidPending() {
	const auto pending = ReadPending();
	if (!pending.valid || !IsTagNewer(pending.tag, CurrentVersion())) {
		ClearPending();
		if (CurrentState == State::Ready) {
			SetState(State::Idle);
		}
		return false;
	}
	const auto exePath = PendingExePath();
	ClearPending();
	LaunchInstallerDelayed(exePath);
	return true;
}
#endif // Q_OS_WIN

} // namespace

void Start() {
#ifdef Q_OS_WIN
	if (Started) {
		return;
	}
	Started = true;
	if (!Manager) {
		Manager = std::make_unique<QNetworkAccessManager>();
	}
	ScheduleNext(kFirstCheckDelay);
#endif // Q_OS_WIN
}

void Stop() {
#ifdef Q_OS_WIN
	CheckTimer.cancel();
	CancelActiveReply();
	if (CurrentState != State::Ready) {
		QFile::remove(PendingExePath());
	}
#endif // Q_OS_WIN
}

rpl::producer<State> StateChanged() {
	return StateEvents.events();
}

bool IsReady() {
	return CurrentState == State::Ready;
}

bool ApplyPendingUpdate() {
#ifdef Q_OS_WIN
	if (!ConsumeValidPending()) {
		return false;
	}
	Core::Quit();
	return true;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

bool CheckPendingAtStartup() {
#ifdef Q_OS_WIN
	if (!ConsumeValidPending()) {
		return false;
	}
	Core::Quit();
	return true;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

} // namespace GithubUpdates
