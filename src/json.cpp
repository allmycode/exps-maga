#include "json.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace expassign::json {
namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  Value ParseDocument() {
    Value v = ParseValue();
    SkipWs();
    if (pos_ != text_.size()) Fail("trailing characters after JSON value");
    return v;
  }

 private:
  [[noreturn]] void Fail(const std::string& what) {
    throw std::invalid_argument("json error at offset " + std::to_string(pos_) +
                                ": " + what);
  }

  void SkipWs() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
            text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  char Peek() {
    if (pos_ >= text_.size()) Fail("unexpected end of input");
    return text_[pos_];
  }

  void Expect(char c) {
    if (Peek() != c) Fail(std::string("expected '") + c + "'");
    ++pos_;
  }

  bool Consume(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  Value ParseValue() {
    SkipWs();
    switch (Peek()) {
      case '{': return ParseObject();
      case '[': return ParseArray();
      case '"': {
        Value v;
        v.type = Value::Type::kString;
        v.str = ParseString();
        return v;
      }
      case 't':
      case 'f': return ParseBool();
      case 'n': return ParseNull();
      default: return ParseNumber();
    }
  }

  Value ParseObject() {
    Expect('{');
    Value v;
    v.type = Value::Type::kObject;
    SkipWs();
    if (Consume('}')) return v;
    while (true) {
      SkipWs();
      std::string key = ParseString();
      SkipWs();
      Expect(':');
      v.object.emplace_back(std::move(key), ParseValue());
      SkipWs();
      if (Consume('}')) return v;
      Expect(',');
    }
  }

  Value ParseArray() {
    Expect('[');
    Value v;
    v.type = Value::Type::kArray;
    SkipWs();
    if (Consume(']')) return v;
    while (true) {
      v.array.push_back(ParseValue());
      SkipWs();
      if (Consume(']')) return v;
      Expect(',');
    }
  }

  Value ParseBool() {
    Value v;
    v.type = Value::Type::kBool;
    if (text_.substr(pos_, 4) == "true") {
      v.boolean = true;
      pos_ += 4;
    } else if (text_.substr(pos_, 5) == "false") {
      v.boolean = false;
      pos_ += 5;
    } else {
      Fail("bad literal");
    }
    return v;
  }

  Value ParseNull() {
    if (text_.substr(pos_, 4) != "null") Fail("bad literal");
    pos_ += 4;
    return Value{};
  }

  Value ParseNumber() {
    size_t start = pos_;
    if (Consume('-')) {}
    while (pos_ < text_.size() &&
           (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
            text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
            text_[pos_] == '+' || text_[pos_] == '-')) {
      ++pos_;
    }
    if (pos_ == start) Fail("expected value");
    std::string token(text_.substr(start, pos_ - start));
    char* end = nullptr;
    double num = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) Fail("bad number '" + token + "'");
    Value v;
    v.type = Value::Type::kNumber;
    v.number = num;
    return v;
  }

  void AppendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  uint32_t ParseHex4() {
    if (pos_ + 4 > text_.size()) Fail("bad \\u escape");
    uint32_t code = 0;
    for (int i = 0; i < 4; ++i) {
      char c = text_[pos_++];
      code <<= 4;
      if (c >= '0' && c <= '9') code |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') code |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') code |= static_cast<uint32_t>(c - 'A' + 10);
      else Fail("bad \\u escape");
    }
    return code;
  }

  std::string ParseString() {
    Expect('"');
    std::string out;
    while (true) {
      if (pos_ >= text_.size()) Fail("unterminated string");
      char c = text_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) Fail("unterminated escape");
      char esc = text_[pos_++];
      switch (esc) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t code = ParseHex4();
          if (code >= 0xD800 && code <= 0xDBFF && pos_ + 1 < text_.size() &&
              text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
            pos_ += 2;
            uint32_t low = ParseHex4();
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          AppendUtf8(out, code);
          break;
        }
        default: Fail("bad escape");
      }
    }
  }

  std::string_view text_;
  size_t pos_ = 0;
};

}  // namespace

const Value* Value::Find(std::string_view key) const {
  for (const auto& [k, v] : object) {
    if (k == key) return &v;
  }
  return nullptr;
}

Value Parse(std::string_view text) { return Parser(text).ParseDocument(); }

}  // namespace expassign::json
