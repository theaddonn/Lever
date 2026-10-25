#pragma once

// Builds elements of the game's settings component variant (Settings::Registry::SettingsFactory result items) in
// memory the game can own and destroy.
//
// The SDK declares Settings::Component<T> as an empty template, so sizeof()/layout of the SDK component classes
// and of std::variant over them are WRONG for the real objects. Static RE of 1.26.51 (docs/RE-settings-screen.md):
// a variant element is allocated with operator new(0x3d0) and the active-alternative index byte sits at +0x3c8
// (verified against the MSVC STL layout: std::variant of 9 alternatives with a 0x3c8-byte largest member has
// sizeof 0x3d0 and the plain index at +0x3c8). The exported constructors ($ctor) write the whole real object, so
// we allocate the real size from the game's allocator, run the real constructor at offset 0 and set the index.
// The returned unique_ptr must be handed to the game (moved into a factory result) and never destroyed on our
// side: the SDK deleter would run the wrong variant destructor.

// All alternatives of the component variant must be complete types wherever std::vector<Element> is destroyed.
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

Element makeText(std::string_view id, std::string_view name, std::optional<std::string> description);
Element makeGroupInfo(std::string_view id, std::string_view name, std::optional<std::string> description);

} // namespace lever::detail::native
