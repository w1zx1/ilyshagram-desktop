/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "owpengram/owpengram_servers.h"

#include <rpl/producer.h>

#include <optional>
#include <vector>

namespace Owpengram {

enum class ServersState {
	Loading,
	Ready,
	Failed,
};

enum class ServersError {
	None,
	Network,
	InvalidContent,
};

// Fetches the operator-managed server list from
// https://raw.githubusercontent.com/w1zx1/ilyshagram-server-list/main/servers.json,
// caches the last successfully applied list in tdata, and keeps it refreshed
// every ten minutes while the app runs. There is no compiled-in server list:
// an app with neither a cache nor a successful fetch has no servers to show.
void StartManagedServers();
void StopManagedServers();
void RequestManagedServersRefresh(bool force = false);

[[nodiscard]] ServersState CurrentServersState();
[[nodiscard]] ServersError CurrentServersError();

// Fires whenever the applied list, a logo, or the loading state changes.
[[nodiscard]] rpl::producer<> ManagedServersChanges();

// The applied list with cosmetic identity filled in from each server's own
// /owpengram/server-info. Empty until the first successful fetch or cache.
[[nodiscard]] std::vector<Server> ManagedServers();
[[nodiscard]] bool IsManagedServerId(const QString &id);

} // namespace Owpengram
