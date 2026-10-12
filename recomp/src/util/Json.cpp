#include "util/Json.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}

    Value ParseDocument()
    {
        Value v = ParseValue();
        SkipWs();
        if (p_ != t_.size()) Fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void Fail(const char* what) const
    {
        std::ostringstream o;
        o << "json: " << what << " at offset " << p_;
        throw std::runtime_error(o.str());
    }

    void SkipWs()
    {
        for (;;) {
            while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' || t_[p_] == '\r')) p_++;
            if (p_ + 1 < t_.size() && t_[p_] == '/' && t_[p_ + 1] == '/') {
                while (p_ < t_.size() && t_[p_] != '\n') p_++;
                continue;
            }
            if (p_ + 1 < t_.size() && t_[p_] == '/' && t_[p_ + 1] == '*') {
                size_t e = t_.find("*/", p_ + 2);
                if (e == std::string::npos) Fail("unterminated comment");
                p_ = e + 2;
                continue;
            }
            return;
        }
    }

    bool Eat(char c)
    {
        SkipWs();
        if (p_ < t_.size() && t_[p_] == c) {
            p_++;
            return true;
        }
        return false;
    }

    Value ParseValue()
    {
        SkipWs();
        if (p_ >= t_.size()) Fail("unexpected end");
        char c = t_[p_];
        Value v;
        if (c == '{') {
            p_++;
            v.kind = Value::Kind::Object;
            while (!Eat('}')) {
                SkipWs();
                std::string key = ParseString();
                if (!Eat(':')) Fail("expected ':'");
                v.obj.emplace_back(std::move(key), ParseValue());
                if (!Eat(',')) {
                    if (!Eat('}')) Fail("expected ',' or '}'");
                    break;
                }
            }
        } else if (c == '[') {
            p_++;
            v.kind = Value::Kind::Array;
            while (!Eat(']')) {
                v.arr.push_back(ParseValue());
                if (!Eat(',')) {
                    if (!Eat(']')) Fail("expected ',' or ']'");
                    break;
                }
            }
        } else if (c == '"') {
            v.kind = Value::Kind::String;
            v.s = ParseString();
        } else if (t_.compare(p_, 4, "true") == 0) {
            p_ += 4;
            v.kind = Value::Kind::Bool;
            v.b = true;
        } else if (t_.compare(p_, 5, "false") == 0) {
            p_ += 5;
            v.kind = Value::Kind::Bool;
        } else if (t_.compare(p_, 4, "null") == 0) {
            p_ += 4;
        } else {
            ParseNumber(v);
        }
        return v;
    }

    void ParseNumber(Value& v)
    {
        size_t start = p_;
        bool isDouble = false;
        while (p_ < t_.size()) {
            char c = t_[p_];
            if (c == '.' || c == 'e' || c == 'E') isDouble = true;
            else if (!(c == '-' || c == '+' || (c >= '0' && c <= '9'))) break;
            p_++;
        }
        if (p_ == start) Fail("unexpected character");
        std::string num = t_.substr(start, p_ - start);
        if (isDouble) {
            v.kind = Value::Kind::Double;
            v.d = std::strtod(num.c_str(), nullptr);
        } else {
            v.kind = Value::Kind::Int;
            v.i = std::strtoll(num.c_str(), nullptr, 10);
        }
    }

    std::string ParseString()
    {
        if (p_ >= t_.size() || t_[p_] != '"') Fail("expected string");
        p_++;
        std::string out;
        while (p_ < t_.size() && t_[p_] != '"') {
            char c = t_[p_++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= t_.size()) Fail("bad escape");
            char e = t_[p_++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (p_ + 4 > t_.size()) Fail("bad \\u escape");
                unsigned cp = static_cast<unsigned>(std::strtoul(t_.substr(p_, 4).c_str(), nullptr, 16));
                p_ += 4;
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += e; break;
            }
        }
        if (p_ >= t_.size()) Fail("unterminated string");
        p_++;
        return out;
    }

    const std::string& t_;
    size_t p_ = 0;
};

void DumpTo(const Value& v, std::ostringstream& o)
{
    switch (v.kind) {
    case Value::Kind::Null: o << "null"; break;
    case Value::Kind::Bool: o << (v.b ? "true" : "false"); break;
    case Value::Kind::Int: o << v.i; break;
    case Value::Kind::Double: o << v.d; break;
    case Value::Kind::String: o << '"' << v.s << '"'; break;
    case Value::Kind::Array:
        o << '[';
        for (size_t k = 0; k < v.arr.size(); k++) {
            if (k) o << ',';
            DumpTo(v.arr[k], o);
        }
        o << ']';
        break;
    case Value::Kind::Object:
        o << '{';
        for (size_t k = 0; k < v.obj.size(); k++) {
            if (k) o << ',';
            o << '"' << v.obj[k].first << "\":";
            DumpTo(v.obj[k].second, o);
        }
        o << '}';
        break;
    }
}

} // namespace

const Value* Value::Find(const std::string& key) const
{
    if (kind != Kind::Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Value& Value::operator[](const std::string& key) const
{
    const Value* v = Find(key);
    if (!v) throw std::runtime_error("json: missing key '" + key + "'");
    return *v;
}

int64_t Value::AsInt() const { return kind == Kind::Double ? static_cast<int64_t>(d) : i; }

double Value::AsDouble() const { return kind == Kind::Int ? static_cast<double>(i) : d; }

std::string Value::Dump() const
{
    std::ostringstream o;
    DumpTo(*this, o);
    return o.str();
}

Value Parse(const std::string& text) { return Parser(text).ParseDocument(); }

bool operator==(const Value& a, const Value& b) { return FirstDifference(a, b).empty(); }

std::string FirstDifference(const Value& a, const Value& b, const std::string& path)
{
    const std::string here = path.empty() ? "<root>" : path;
    if (a.kind != b.kind) return here;
    switch (a.kind) {
    case Value::Kind::Null: return "";
    case Value::Kind::Bool: return a.b == b.b ? "" : here;
    case Value::Kind::Int: return a.i == b.i ? "" : here;
    case Value::Kind::Double: return a.d == b.d ? "" : here;
    case Value::Kind::String: return a.s == b.s ? "" : here;
    case Value::Kind::Array:
        if (a.arr.size() != b.arr.size()) return here + ".length";
        for (size_t k = 0; k < a.arr.size(); k++) {
            std::string d = FirstDifference(a.arr[k], b.arr[k], path + "[" + std::to_string(k) + "]");
            if (!d.empty()) return d;
        }
        return "";
    case Value::Kind::Object:
        if (a.obj.size() != b.obj.size()) return here + ".keys";
        for (const auto& kv : a.obj) {
            const Value* other = b.Find(kv.first);
            if (!other) return path + "." + kv.first;
            std::string d = FirstDifference(kv.second, *other, path.empty() ? kv.first : path + "." + kv.first);
            if (!d.empty()) return d;
        }
        return "";
    }
    return here;
}

} // namespace json
