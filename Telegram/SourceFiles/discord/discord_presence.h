#pragma once

#include <QString>

namespace DiscordRpc {

void Start(const QString &clientId);
void StartDefault();
void Stop();

void UpdateDefaultPresence();
void UpdatePresence(
	const QString &details,
	const QString &state,
	const QString &largeImageKey,
	const QString &largeImageText,
	const QString &buttonLabel,
	const QString &buttonUrl);

void ClearPresence();
void Pump();
bool IsRunning();

} // namespace DiscordRpc
