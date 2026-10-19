#pragma once
#include <string>
#include <string_view>

namespace mt4copier {
// Quote data from account/request fields so a terminal comment cannot create
// a second log line or masquerade as another diagnostic field. UTF-8 remains
// readable; this never changes the native request itself.
inline std::string DiagnosticText(std::string_view value) {
    constexpr char hex[]="0123456789abcdef";
    std::string result="\"";
    for (unsigned char c:value) {
        if (c=='\\' || c=='\"') { result+='\\'; result+=static_cast<char>(c); }
        else if (c<0x20 || c==0x7f) { result+="\\x"; result+=hex[c>>4]; result+=hex[c&15]; }
        else result+=static_cast<char>(c);
    }
    result+='\"'; return result;
}
} // namespace mt4copier
