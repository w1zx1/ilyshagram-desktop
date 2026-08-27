/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

namespace Branding {

constexpr auto AppName = "ilyshaGram Desktop"_cs;
constexpr auto ShortAppName = "ilyshaGram"_cs;
constexpr auto ApplicationName = "ilyshaGramDesktop"_cs;
constexpr auto CompanyName = "ilyshaGram"_cs;

#ifdef _DEBUG
constexpr auto WindowsIconPath = "Resources/OwpenGram/art/icon256_debug.ico"_cs;
#else
constexpr auto WindowsIconPath = "Resources/OwpenGram/art/icon256.ico"_cs;
#endif
constexpr auto MacOSIconName = "Icon.icns"_cs;
constexpr auto LinuxIconBase = "Resources/OwpenGram/art/icon"_cs;

#ifdef _DEBUG
constexpr auto LogoPath = ":/gui/art/logo_256_debug.png"_cs;
constexpr auto LogoNoMarginPath = ":/gui/art/logo_256_debug.png"_cs;
#else
constexpr auto LogoPath = ":/gui/art/logo_256.png"_cs;
constexpr auto LogoNoMarginPath = ":/gui/art/logo_256_no_margin.png"_cs;
#endif
constexpr auto PlaneWhitePath = ":/gui/plane_white.svg"_cs;
constexpr auto BusinessLogoPath = ":/gui/art/business_logo.png"_cs;
constexpr auto AffiliateLogoPath = ":/gui/art/affiliate_logo.png"_cs;

constexpr auto BundleIdentifier = "org.owpengram.desktop"_cs;

#ifdef _DEBUG
constexpr auto LinuxAppId = "org.owpengram.desktop.Debug"_cs;
#else
constexpr auto LinuxAppId = "org.owpengram.desktop"_cs;
#endif

} // namespace Branding
