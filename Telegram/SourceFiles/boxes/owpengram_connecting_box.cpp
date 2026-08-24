/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_connecting_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kDotsIntervalMs = crl::time(400);
constexpr auto kMaxDots = 3;

} // namespace

ConnectingBox::ConnectingBox(QWidget*, Fn<void()> onFailedClose)
: _onFailedClose(std::move(onFailedClose))
, _content(this) {
}

void ConnectingBox::prepare() {
	// While connecting the box must block every interaction.
	setCloseByEscape(false);
	setCloseByOutsideClick(false);

	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::boxLittleSkip));
	_label = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			QString(),
			st::introErrorCentered),
		st::boxRowPadding);
	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::boxLittleSkip));

	updateText();

	_dotsTimer.setCallback([=] {
		_dots = (_dots + 1) % (kMaxDots + 1);
		updateText();
	});
	_dotsTimer.callEach(kDotsIntervalMs);

	boxClosing(
	) | rpl::on_next([=] {
		if (_failed && _onFailedClose) {
			_onFailedClose();
		}
	}, lifetime());

	setDimensionsToContent(st::boxWidth, _content);
}

void ConnectingBox::updateText() {
	if (!_label) {
		return;
	}
	if (_failed) {
		_label->setText(tr::lng_owpengram_server_connect_failed(tr::now));
	} else {
		_label->setText(tr::lng_owpengram_server_connecting(tr::now) + QString(_dots, '.'));
	}
}

void ConnectingBox::showFailed() {
	if (_failed) {
		return;
	}
	_failed = true;
	_dotsTimer.cancel();
	updateText();

	// Now allow the user to dismiss the box, but stay on the current screen.
	setCloseByEscape(true);
	setCloseByOutsideClick(true);
	addButton(tr::lng_close(), [=] { closeBox(); });
}
