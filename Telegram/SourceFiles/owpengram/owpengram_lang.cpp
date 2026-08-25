/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/owpengram_lang.h"

#include <array>

#include "lang/lang_instance.h"

namespace Owpengram {
namespace {

struct RebrandOverride {
	const char *key;
	const char16_t *en;
	const char16_t *ru;
};

const auto kRebrandOverrides = std::array<RebrandOverride, 16>{ {
	{ "lng_open_from_tray", u"Open ilyshaGram", u"Открыть ilyshaGram" },
	{ "lng_quit_from_tray", u"Quit ilyshaGram", u"Закрыть ilyshaGram" },
	{ "lng_tray_icon_text",
		u"ilyshaGram is still running here,\nyou can change this in Settings.\nIf it disappears from the tray,\nyou can drag it back from the hidden icons.",
		u"ilyshaGram всё ещё запущен здесь,\nэто можно изменить в настройках.\nЕсли он исчезнет из трея,\nвы можете вернуть его из скрытых значков." },
	{ "lng_local_storage_device_telegram", u"ilyshaGram cache", u"Кэш ilyshaGram" },
	{ "lng_local_storage_device_usage",
		u"ilyshaGram uses {percent} of your device storage.",
		u"ilyshaGram использует {percent} памяти вашего устройства." },
	{ "lng_settings_faq", u"ilyshaGram FAQ", u"Справка ilyshaGram" },
	{ "lng_settings_features", u"ilyshaGram Features", u"Возможности ilyshaGram" },
	{ "lng_message_unsupported",
		u"This message is not supported by your version of ilyshaGram. Please update to the latest version in Settings > Advanced, or install it from {link}. If you are already using the latest version, this message might depend on a feature that is not yet implemented.",
		u"Это сообщение не поддерживается вашей версией ilyshaGram. Пожалуйста, обновитесь до последней версии в Настройки > Дополнительно или установите её из {link}. Если вы уже используете последнюю версию, это сообщение может зависеть от функции, которая ещё не реализована." },
	{ "lng_gift_transfer_unlocks_update_about",
		u"Please update your ilyshaGram application to the latest version.",
		u"Пожалуйста, обновите ваше приложение ilyshaGram до последней версии." },
	{ "lng_group_call_mac_accessibility",
		u"Please allow **Accessibility** for ilyshaGram in Privacy Settings.\n\nYou may need to restart the app.",
		u"Пожалуйста, разрешите **Accessibility** для ilyshaGram в настройках приватности.\n\nВозможно, потребуется перезапустить приложение." },
	{ "lng_no_mic_permission",
		u"ilyshaGram needs microphone access so that you can make calls and record voice messages.",
		u"ilyshaGram требует доступ к микрофону, чтобы вы могли звонить и записывать голосовые сообщения." },
	{ "lng_terms_delete_warning",
		u"Warning, this will irreversibly delete your Telegram account and all the data you store in the Telegram cloud.\n\nImportant: You can Cancel now and export your data first instead of losing it. (To do this, open the latest version of ilyshaGram Desktop and go to Settings > Advanced > Export ilyshaGram data.)",
		u"Внимание: это безвозвратно удалит ваш аккаунт Telegram и все данные, которые вы храните в облаке Telegram.\n\nВажно: вы можете нажать Отмена и сначала экспортировать свои данные, чтобы не потерять их. (Чтобы сделать это, откройте последнюю версию ilyshaGram и перейдите в Настройки > Дополнительно > Экспорт данных ilyshaGram.)" },
	{ "lng_passport_app_out_of_date",
		u"Sorry, your ilyshaGram app is out of date and can't handle this request. Please update ilyshaGram.",
		u"Извините, ваше приложение ilyshaGram устарело и не может выполнить этот запрос. Пожалуйста, обновите ilyshaGram." },
	{ "lng_export_progress",
		u"You can close this window now. Please don't quit ilyshaGram until the data export is completed.",
		u"Теперь можно закрыть это окно. Не закрывайте ilyshaGram, пока экспорт данных не завершится." },
	{ "lng_screen_reader_bar_text",
		u"ilyshaGram is working in Screen Reader mode.",
		u"ilyshaGram работает в режиме Screen Reader." },
	{ "lng_stories_unsupported",
		u"This story is not supported\nby your version of ilyshaGram.",
		u"Эта история не поддерживается\nвашей версией ilyshaGram." },
} };

} // namespace

void ApplyLangRebrandOverrides() {
	auto &lang = Lang::GetInstance();
	const auto useRu = lang.id().startsWith(QLatin1String("ru"));
	for (const auto &override : kRebrandOverrides) {
		const auto *value = useRu ? override.ru : override.en;
		lang.applyValue(
			QByteArray(override.key),
			QString::fromUtf16(value).toUtf8());
	}
}

} // namespace Owpengram
