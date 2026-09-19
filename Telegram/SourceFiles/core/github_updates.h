/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include <rpl/producer.h>

namespace GithubUpdates {

enum class State {
	Idle,
	Checking,
	Downloading,
	Ready,
	Failed,
};

void Start();
void Stop();

[[nodiscard]] rpl::producer<State> StateChanged();
[[nodiscard]] bool IsReady();

// Launches the downloaded installer (if valid) and quits the app.
// Returns true if an installation was started.
bool ApplyPendingUpdate();

// Startup hook: launches a pending installer left from the previous
// session (if valid) and returns true if the caller should quit.
bool CheckPendingAtStartup();

} // namespace GithubUpdates
