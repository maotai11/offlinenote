// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace LegacyNoteTextCodec {

std::string encode(std::string_view text);
std::string decode(std::string_view text);

} // namespace LegacyNoteTextCodec
