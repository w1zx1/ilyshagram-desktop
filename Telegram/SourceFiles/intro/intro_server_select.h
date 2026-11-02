/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "intro/intro_step.h"
#include "owpengram/owpengram_servers.h"

namespace Ui {
class RoundButton;
class RpWidget;
class ScrollArea;
class FlatLabel;
} // namespace Ui

namespace Intro {
namespace details {

class ServerSelectWidget final : public Step {
public:
	ServerSelectWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void finishInit() override;
	void activate() override;
	void submit() override;
	[[nodiscard]] rpl::producer<QString> nextButtonText() const override;
	[[nodiscard]] bool hasBack() const override {
		return true;
	}

	void resizeEvent(QResizeEvent *e) override;
	void showEvent(QShowEvent *e) override;

	[[nodiscard]] int contentLeft() const override;

private:
	void rebuildList();
	void joinServer(const Owpengram::Server &server);
	void proceedJoin(const Owpengram::Server &server);
	void updateListGeometry();
	void updateRowsGeometry();
	[[nodiscard]] int listWidth() const;
	[[nodiscard]] int listInnerWidth() const;
	[[nodiscard]] int listPanelHeight(int panelTop) const;

	const not_null<Ui::RpWidget*> _panel;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<Ui::RpWidget*> _rowsContainer;
	const not_null<Ui::FlatLabel*> _ownTitle;
	const not_null<Ui::FlatLabel*> _ownDescription;
	const not_null<Ui::FlatLabel*> _emptyLabel;
	const not_null<Ui::RoundButton*> _retryButton;
	object_ptr<Ui::RoundButton> _addServer;
	base::Timer _statusTimer;
};

} // namespace details
} // namespace Intro
