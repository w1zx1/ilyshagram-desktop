/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_add_server_box.h"

#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kBoxWidth = 700;
constexpr auto kPortFieldWidth = 90;
constexpr auto kAvatarGap = 14;

// ── Avatar circle picker ──────────────────────────────────────────────────

class ServerLogoPicker final : public Ui::RippleButton {
public:
	ServerLogoPicker(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose);

	void refresh() { update(); }

private:
	void paintEvent(QPaintEvent *e) override;

	const QImage *_preview = nullptr;
};

ServerLogoPicker::ServerLogoPicker(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose)
: RippleButton(parent, st::defaultRippleAnimation)
, _preview(preview) {
	const auto sz = st::introServerAddLogoSize;
	resize(sz, sz);
	setClickedCallback(std::move(choose));
}

void ServerLogoPicker::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	PainterHighQualityEnabler hq(p);
	paintRipple(p, 0, 0);

	const auto sz = st::introServerAddLogoSize;
	p.setPen(Qt::NoPen);

	if (_preview->isNull()) {
		// Placeholder: neutral circle + "+" to invite the user to pick a photo.
		p.setBrush(st::windowBgOver);
		p.drawEllipse(0, 0, sz, sz);
		const auto arm = sz / 4;
		const auto cx = sz / 2;
		const auto cy = sz / 2;
		p.setPen(QPen(st::windowSubTextFg, 2, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(cx - arm, cy, cx + arm, cy);
		p.drawLine(cx, cy - arm, cx, cy + arm);
	} else {
		p.setBrush(st::boxBg);
		p.drawEllipse(0, 0, sz, sz);
		p.setClipRegion(QRegion(0, 0, sz, sz, QRegion::Ellipse));
		const auto pixmap = QPixmap::fromImage(*_preview).scaled(
			sz, sz,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		p.drawPixmap((sz - pixmap.width()) / 2, (sz - pixmap.height()) / 2, pixmap);
	}
}

// ── Top row: [avatar] [name / description] ────────────────────────────────
// Floating placeholders inside InputField act as labels — no separate FlatLabel.

class AvatarNameDescRow final : public Ui::RpWidget {
public:
	AvatarNameDescRow(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose);

	[[nodiscard]] Ui::InputField *name() const { return _name; }
	[[nodiscard]] Ui::InputField *desc() const { return _desc; }
	void refreshPreview() { _picker->refresh(); }

protected:
	int resizeGetHeight(int newWidth) override;

private:
	ServerLogoPicker *_picker  = nullptr;
	Ui::FlatLabel    *_iconLbl = nullptr;
	Ui::InputField   *_name    = nullptr;
	Ui::InputField   *_desc    = nullptr;
};

AvatarNameDescRow::AvatarNameDescRow(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose)
: RpWidget(parent) {
	_picker = Ui::CreateChild<ServerLogoPicker>(this, preview, std::move(choose));
	_iconLbl = Ui::CreateChild<Ui::FlatLabel>(
		this,
		tr::lng_owpengram_server_icon(tr::now),
		st::introServerAddIconCaption);
	_name = Ui::CreateChild<Ui::InputField>(
		this,
		st::defaultInputField,
		tr::lng_owpengram_server_name());
	_desc = Ui::CreateChild<Ui::InputField>(
		this,
		st::defaultInputField,
		tr::lng_owpengram_server_description());
}

int AvatarNameDescRow::resizeGetHeight(int newWidth) {
	const auto sz = st::introServerAddLogoSize;
	const auto rightX = sz + kAvatarGap;
	const auto rightW = std::max(newWidth - rightX, 1);

	_name->resizeToWidth(rightW);
	_name->moveToLeft(rightX, 0);
	const auto descY = _name->height() + 8;
	_desc->resizeToWidth(rightW);
	_desc->moveToLeft(rightX, descY);
	const auto rightH = descY + _desc->height();

	_iconLbl->resizeToWidth(sz);
	const auto lblGap = 4;
	const auto lblH = _iconLbl->height();
	const auto leftH = sz + lblGap + lblH;
	const auto totalH = std::max(rightH, leftH);

	const auto leftTopY = (totalH - leftH) / 2;
	_picker->resize(sz, sz);
	_picker->moveToLeft(0, leftTopY);
	_iconLbl->moveToLeft(0, leftTopY + sz + lblGap);

	return totalH;
}

// ── Host + Port row ────────────────────────────────────────────────────────
// Two fields side-by-side; floating placeholders serve as labels.

class HostPortRow final : public Ui::RpWidget {
public:
	explicit HostPortRow(QWidget *parent);

	[[nodiscard]] Ui::InputField *host() const { return _host; }
	[[nodiscard]] Ui::InputField *port() const { return _port; }

protected:
	int resizeGetHeight(int newWidth) override;

private:
	Ui::InputField *_host = nullptr;
	Ui::InputField *_port = nullptr;
};

HostPortRow::HostPortRow(QWidget *parent) : RpWidget(parent) {
	_host = Ui::CreateChild<Ui::InputField>(
		this,
		st::defaultInputField,
		tr::lng_owpengram_server_host_hint());
	_port = Ui::CreateChild<Ui::InputField>(
		this,
		st::defaultInputField,
		tr::lng_owpengram_server_port());
}

int HostPortRow::resizeGetHeight(int newWidth) {
	const auto portW = kPortFieldWidth;
	const auto hostW = std::max(newWidth - portW - 8, 1);
	_host->resizeToWidth(hostW);
	_host->moveToLeft(0, 0);
	_port->resizeToWidth(portW);
	_port->moveToLeft(hostW + 8, 0);
	return _host->height();
}

// ── Side-by-side radio buttons ────────────────────────────────────────────

class RadioTypeRow final : public Ui::RpWidget {
public:
	RadioTypeRow(
		QWidget *parent,
		std::shared_ptr<Ui::RadiobuttonGroup> group);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	Ui::Radiobutton *_single = nullptr;
	Ui::Radiobutton *_multi  = nullptr;
};

RadioTypeRow::RadioTypeRow(
		QWidget *parent,
		std::shared_ptr<Ui::RadiobuttonGroup> group)
: RpWidget(parent) {
	_single = Ui::CreateChild<Ui::Radiobutton>(
		this, group, 0, tr::lng_owpengram_server_single(tr::now));
	_multi = Ui::CreateChild<Ui::Radiobutton>(
		this, group, 2, tr::lng_owpengram_server_multi(tr::now));
}

int RadioTypeRow::resizeGetHeight(int newWidth) {
	const auto gap = 12;
	const auto half = std::max((newWidth - gap) / 2, 1);
	_single->resizeToWidth(half);
	_single->moveToLeft(0, 0);
	_multi->resizeToWidth(half);
	_multi->moveToLeft(half + gap, 0);
	return std::max(_single->height(), _multi->height());
}

} // namespace

// ── AddServerBox ──────────────────────────────────────────────────────────

AddServerBox::AddServerBox(
	QWidget*,
	Fn<void(Owpengram::Server)> done,
	Owpengram::Server existing)
: _done(std::move(done))
, _content(this)
, _typeGroup(std::make_shared<Ui::RadiobuttonGroup>(0)) {

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── avatar + name + description ──────────────────────────────────────
	const auto topRow = _content->add(
		object_ptr<AvatarNameDescRow>(
			_content,
			&_logoPreview,
			[=] { chooseLogo(); }),
		st::boxRowPadding);
	_refreshAvatar = [topRow] { topRow->refreshPreview(); };
	_name        = topRow->name();
	_description = topRow->desc();

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── host + port ───────────────────────────────────────────────────────
	const auto hostPortRow = _content->add(
		object_ptr<HostPortRow>(_content),
		st::boxRowPadding);
	_host      = hostPortRow->host();
	_portField = hostPortRow->port();

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── server type ───────────────────────────────────────────────────────
	_content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			tr::lng_owpengram_server_type(tr::now),
			st::boxLabel),
		st::boxRowPadding);
	_content->add(object_ptr<Ui::FixedHeightWidget>(_content, 6));
	_content->add(
		object_ptr<RadioTypeRow>(_content, _typeGroup),
		st::boxRowPadding);

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip / 2));

	// ── main DC (single-server only) ─────────────────────────────────────
	_mainDcWrap = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	{
		const auto inner = _mainDcWrap->entity();
		inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_owpengram_server_main_dc(tr::now),
				st::boxLabel),
			st::boxRowPadding);
		_mainDcField = inner->add(
			object_ptr<Ui::InputField>(
				inner,
				st::defaultInputField,
				rpl::single(u"1..5"_q),
				u"2"_q),
			st::boxRowPadding);
		inner->add(object_ptr<Ui::FixedHeightWidget>(
			inner,
			st::introServerAddSectionSkip / 2));
	}

	// ── RSA key ───────────────────────────────────────────────────────────
	_content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			tr::lng_owpengram_server_rsa_key(tr::now),
			st::boxLabel),
		st::boxRowPadding);
	_rsaPublicKey = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_owpengram_server_rsa_key_hint()),
		st::boxRowPadding);

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── toggle main DC visibility ─────────────────────────────────────────
	_typeGroup->setChangedCallback([=](int value) {
		_mainDcWrap->toggle(value == 0, anim::type::normal);
	});

	// ── pre-fill when editing an existing server ──────────────────────────
	if (existing.valid()) {
		_editingId = existing.id;
		_name->setText(existing.name);
		_description->setText(existing.description);
		_host->setText(existing.host);
		if (existing.port > 0) {
			_portField->setText(QString::number(existing.port));
		}
		_typeGroup->setValue(existing.multiDc ? 2 : 0);
		if (existing.mainDcId > 0) {
			_mainDcField->setText(QString::number(existing.mainDcId));
		}
		_rsaPublicKey->setText(existing.rsaPublicKey);
		_mainDcWrap->toggle(!existing.multiDc, anim::type::instant);
	}
}

void AddServerBox::prepare() {
	setTitle(_editingId.isEmpty()
		? tr::lng_owpengram_server_add_title()
		: tr::lng_owpengram_server_edit_title());
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	setDimensionsToContent(kBoxWidth, _content);
}

void AddServerBox::setInnerFocus() {
	if (_name) {
		_name->setFocusFast();
	}
}

void AddServerBox::chooseLogo() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty()) {
			return;
		}
		auto image = Images::Read({
			.path = result.paths.front(),
			.forceOpaque = true,
		}).image;
		if (image.isNull()) {
			Ui::Toast::Show(tr::lng_owpengram_server_logo_invalid(tr::now));
			return;
		}
		_logoSourcePath = result.paths.front();
		_logoPreview = std::move(image);
		if (_refreshAvatar) {
			_refreshAvatar();
		}
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_owpengram_server_logo_choose(tr::now),
		FileDialog::ImagesFilter(),
		crl::guard(this, callback));
}

void AddServerBox::save() {
	const auto name = _name->getLastText().trimmed();
	auto host = _host->getLastText().trimmed();
	auto port = _portField->getLastText().toInt();
	const auto rsaPublicKey = _rsaPublicKey->getLastText().trimmed();
	const auto description = _description->getLastText().trimmed();
	const auto multiDc = (_typeGroup->current() == 2);
	const auto mainDcId = (!multiDc && _mainDcField)
		? _mainDcField->getLastText().trimmed().toInt()
		: 0;

	// Allow "host:port" shorthand in the host field.
	if (host.contains(':')) {
		const auto parts = host.split(':');
		if (parts.size() == 2 && port <= 0) {
			host = parts[0].trimmed();
			port = parts[1].trimmed().toInt();
		}
	}

	if (name.isEmpty()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_name->setFocusFast();
		return;
	}
	if (host.isEmpty()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_host->setFocusFast();
		return;
	}
	if (port <= 0) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_portField->setFocusFast();
		return;
	}
	if (!Owpengram::IsValidRsaPublicKeyPem(rsaPublicKey)) {
		Ui::Toast::Show(tr::lng_owpengram_server_rsa_key_invalid(tr::now));
		_rsaPublicKey->setFocusFast();
		return;
	}

	if (!_editingId.isEmpty()) {
		Owpengram::RemoveCustomServer(_editingId);
	}
	if (const auto server = Owpengram::AddCustomServer(
			name,
			host,
			port,
			description,
			rsaPublicKey,
			_logoSourcePath,
			multiDc,
			mainDcId)) {
		if (_done) {
			_done(*server);
		}
		closeBox();
	}
}
