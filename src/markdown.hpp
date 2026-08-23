#pragma once

#include <string>
#include <string_view>

std::string markdown_to_html(std::string_view md);
std::string markdown_to_text(std::string_view md);
