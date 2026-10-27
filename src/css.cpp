#include "css.hpp"

#include <array>
#include <cctype>
#include <cstdint>

namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

std::array<uint8_t, 32> sha256(std::string_view data) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string msg(data);
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back('\0');
  const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bits >> (i * 8)) & 0xff));

  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      const auto* p = reinterpret_cast<const unsigned char*>(msg.data()) + off + i * 4;
      w[i] = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                          kK[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  std::array<uint8_t, 32> out{};
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - j * 8));
  }
  return out;
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
         static_cast<unsigned char>(c) >= 0x80;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

}  // namespace

std::string sha256_hex(std::string_view data) {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (uint8_t b : sha256(data)) {
    out += hex[b >> 4];
    out += hex[b & 15];
  }
  return out;
}

std::string sha256_base64(std::string_view data) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto d = sha256(data);
  std::string out;
  for (size_t i = 0; i < d.size(); i += 3) {
    const uint32_t n = (uint32_t{d[i]} << 16) | (i + 1 < d.size() ? uint32_t{d[i + 1]} << 8 : 0) |
                       (i + 2 < d.size() ? d[i + 2] : 0);
    out += tbl[(n >> 18) & 63];
    out += tbl[(n >> 12) & 63];
    out += i + 1 < d.size() ? tbl[(n >> 6) & 63] : '=';
    out += i + 2 < d.size() ? tbl[n & 63] : '=';
  }
  return out;
}

std::string minify_css(std::string_view s) {
  std::string out;
  bool space = false;  // whitespace (or a dropped comment) seen since the last token
  auto put = [&](char c) {
    if (space && !out.empty()) {
      const char last = out.back();
      const bool tight_before = c == '{' || c == '}' || c == ';' || c == ',' || c == ')';
      const bool tight_after = last == '{' || last == '}' || last == ';' || last == ',' ||
                               last == ':' || last == '(';
      if (!tight_before && !tight_after) out.push_back(' ');
    }
    space = false;
    out.push_back(c);
  };

  for (size_t i = 0; i < s.size();) {
    const char c = s[i];
    if (is_space(c)) {
      space = true;
      ++i;
    } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      size_t end = s.find("*/", i + 2);
      end = end == std::string_view::npos ? s.size() : end + 2;
      if (i + 2 < s.size() && s[i + 2] == '!') {  // /*! ... */ is kept as written
        if (space && !out.empty() && out.back() != '}' && out.back() != '{' && out.back() != ';') {
          out.push_back(' ');
        }
        space = false;
        out.append(s.substr(i, end - i));
      } else {
        space = true;
      }
      i = end;
    } else if (c == '"' || c == '\'') {
      put(c);
      ++i;
      while (i < s.size() && s[i] != c && s[i] != '\n') {
        if (s[i] == '\\' && i + 1 < s.size()) out.push_back(s[i++]);
        out.push_back(s[i++]);
      }
      if (i < s.size() && s[i] == c) out.push_back(s[i++]);
    } else if (c == '\\' && i + 1 < s.size()) {
      put(c);  // an escape: the next character is not syntax and not collapsible
      out.push_back(s[i + 1]);
      i += 2;
    } else if ((c == 'u' || c == 'U') && i + 3 < s.size() && (i == 0 || !is_ident_char(s[i - 1])) &&
               (s[i + 1] == 'r' || s[i + 1] == 'R') && (s[i + 2] == 'l' || s[i + 2] == 'L') &&
               s[i + 3] == '(') {
      // url(...): an unquoted address is copied whole; a quoted one is a string next round.
      put(c);
      out.append(s.substr(i + 1, 3));
      i += 4;
      size_t k = i;
      while (k < s.size() && is_space(s[k])) ++k;
      if (k < s.size() && s[k] != '"' && s[k] != '\'') {
        const size_t close = s.find(')', k);
        const size_t stop = close == std::string_view::npos ? s.size() : close;
        size_t e = stop;
        while (e > k && is_space(s[e - 1])) --e;
        out.append(s.substr(k, e - k));
        i = stop;
      }
    } else if (c == '}') {
      // The last declaration's ; is optional.
      while (out.size() > 1 && out.back() == ';' && out[out.size() - 2] != '\\') {
        out.pop_back();
      }
      put(c);
      ++i;
    } else {
      put(c);
      ++i;
    }
  }
  return out;
}
