#pragma once

// Builds elements of the game's settings component variant (Settings::Registry::SettingsFactory result items) in
// memory the game can own and destroy, for Minecraft Bedrock 1.26.51 (every offset below was verified against
// that build).
//
// The SDK declares Settings::Component<T> as an empty template, so sizeof()/layout of the SDK component classes and
// of std::variant over them are WRONG for the real objects. Real layout: a variant element is 0x3d0 bytes, the
// alternative index byte sits at +0x3c8; the Component base is 0x1e0 bytes (vtable, id string +0x08, name string
// +0x28, SettingsType +0x48, optional<string> description +0x50, two std::functions, a pub/sub object); the derived
// fields start at +0x1e0 in the SDK's declaration order.
//
// Text, GroupInfo, Option and Action have exported constructors and are built with them. Boolean, Number<float>,
// String and Banner have none: they are "hand built" (base via the exported TextComponent constructor, then the
// real vtable, the type and the derived fields written in place). Data providers are our own classes with the
// game's vtable order, allocated and freed on our side through their virtual destructor.
//
// A returned element must be handed to the game (moved into a factory result) and never destroyed on our side:
// the SDK deleter would run the wrong variant destructor.

#include "internal/Model.h"

// All alternatives of the component variant must be complete types wherever std::vector<Element> is destroyed.
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/settings/ActionComponent.h"
#include "mc/client/settings/BannerComponent.h"
#include "mc/client/settings/BooleanComponent.h"
#include "mc/client/settings/GroupInfoComponent.h"
#include "mc/client/settings/IActionDataProvider.h"
#include "mc/client/settings/IBooleanDataProvider.h"
#include "mc/client/settings/IDataProvider.h"
#include "mc/client/settings/INumberDataProvider.h"
#include "mc/client/settings/IOptionsDataProvider.h"
#include "mc/client/settings/IStringDataProvider.h"
#include "mc/client/settings/NumberComponent.h"
#include "mc/client/settings/OptionComponent.h"
#include "mc/client/settings/Registry.h"
#include "mc/client/settings/StringComponent.h"
#include "mc/client/settings/TextComponent.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lever::detail::native {

using Factory = ::Settings::Registry::SettingsFactory;
using Element = Factory::result_type::value_type; // std::unique_ptr<std::variant<...components...>>

/// Identifies a setting of the host model; providers resolve it on every call so a mod that unregistered (and
/// may have unloaded) is never called back.
struct SettingKey {
    std::string modId, pageId, settingId;
};

Element makeText(std::string_view id, std::string_view name, std::optional<std::string> description);
Element makeGroupInfo(std::string_view id, std::string_view name, std::optional<std::string> description);
Element makeToggle(std::string_view id, const Setting& s, SettingKey key);
Element makeSlider(std::string_view id, const Setting& s, SettingKey key);
Element makeDropdown(std::string_view id, const Setting& s, SettingKey key, bool asToggle = false);
Element makeTextField(std::string_view id, const Setting& s, SettingKey key);
Element makeButton(std::string_view id, const Setting& s, SettingKey key);
Element makeBanner(std::string_view id, const Setting& s);

/// Builds the element for a model setting (nullptr when the kind cannot be rendered on this game version).
Element makeSetting(std::string_view id, const Setting& s, SettingKey key);

/// The game's option registry (from the builder context); needed once to discover the Boolean vtable.
void setOptionRegistry(::IOptionRegistry* options);

/// Pushes the descriptions registered since the last call into the game's I18n (call after a factory run).
void flushTranslations();

/// true when hand-built toggles are available (the BooleanComponent vtable is known for this build).
bool togglesSupported();

/// Fires the change listener of every live provider whose key matches (empty pageId / settingId = any), which
/// makes the game's settings queries re-read the values. Game thread only.
void notifyChanged(std::string_view modId, std::string_view pageId, std::string_view settingId);

} // namespace lever::detail::native
