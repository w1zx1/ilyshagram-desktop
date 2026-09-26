/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_ilysha.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/github_updates.h"
#include "core/version.h"
#include "discord/discord_presence.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

void BuildDiscordSection(SectionBuilder &builder) {
	const auto settings = &Core::App().settings();

	builder.addSkip();

	const auto discordRpc = builder.addCheckbox({
		.id = u"ilysha/discord_rpc"_q,
		.title = tr::lng_settings_discord_rpc(),
		.checked = settings->discordRpcEnabled(),
		.keywords = { u"discord"_q, u"rpc"_q, u"rich presence"_q },
	});
	if (discordRpc) {
		discordRpc->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != settings->discordRpcEnabled());
		}) | rpl::on_next([=](bool checked) {
			settings->setDiscordRpcEnabled(checked);
			Core::App().saveSettingsDelayed();
			if (checked) {
				DiscordRpc::StartDefault();
			} else {
				DiscordRpc::Stop();
			}
		}, discordRpc->lifetime());
	}

	builder.addSkip();
}

#ifdef Q_OS_WIN
void BuildUpdatesSection(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"ilysha/version"_q,
		.title = tr::lng_settings_version_info(),
		.keywords = { u"version"_q, u"update"_q, u"check"_q },
	});

	const auto version = u"Сейчас установлена версия %1"_q.arg(
		QString::fromLatin1(IlyshaVersionStr));
	auto status = rpl::single(
		GithubUpdates::IsReady()
			? GithubUpdates::State::Ready
			: GithubUpdates::State::Idle
	) | rpl::then(
		GithubUpdates::StateChanged()
	) | rpl::map([=](GithubUpdates::State state) {
		using State = GithubUpdates::State;
		switch (state) {
		case State::Checking:
			return tr::lng_settings_update_checking(tr::now);
		case State::Downloading:
			return tr::lng_settings_downloading_update(
				tr::now,
				lt_progress,
				u"…"_q);
		case State::Ready:
			return tr::lng_settings_update_ready(tr::now);
		case State::Failed:
			return tr::lng_settings_update_fail(tr::now);
		case State::UpToDate:
			return u"Установлена последняя версия"_q;
		case State::Idle:
			break;
		}
		return version;
	});

	if (const auto button = builder.addButton({
		.id = u"ilysha/check_for_updates"_q,
		.title = tr::lng_settings_check_now(),
		.icon = { &st::menuIconDownload },
		.onClick = [] {
			GithubUpdates::CheckNow();
		},
		.keywords = { u"update"_q, u"check"_q, u"version"_q, u"new"_q },
	})) {
		const auto padding = button->st().padding;
		const auto tall = st::settingsUpdateToggle.padding.top()
			+ st::settingsUpdateToggle.height
			+ st::settingsUpdateToggle.padding.bottom();
		button->setPaddingOverride(style::margins(
			padding.left(),
			st::settingsUpdateToggle.padding.top(),
			padding.right(),
			tall
				- st::settingsUpdateToggle.padding.top()
				- button->st().height));

		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			st::settingsUpdateState);
		label->show();
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		rpl::combine(
			button->widthValue(),
			std::move(status)
		) | rpl::on_next([=](
				int width,
				const QString &text) {
			label->setText(text);
			label->resizeToNaturalWidth(
				width - padding.left() - padding.right());
			label->moveToLeft(
				padding.left(),
				st::settingsUpdateStatePosition.y());
		}, label->lifetime());
	}

	builder.addSkip();
}
#endif // Q_OS_WIN

class Ilysha : public Section<Ilysha> {
public:
	Ilysha(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = Ilysha::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_ilysha,
	.icon = &st::menuIconExperimental,
}, [](SectionBuilder &builder) {
	BuildDiscordSection(builder);
#ifdef Q_OS_WIN
	BuildUpdatesSection(builder);
#endif // Q_OS_WIN
});

const SectionBuildMethod kIlyshaSection = kMeta.build;

Ilysha::Ilysha(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Ilysha::title() {
	return tr::lng_settings_ilysha();
}

void Ilysha::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kIlyshaSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Type IlyshaId() {
	return Ilysha::Id();
}

} // namespace Settings
