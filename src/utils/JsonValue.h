#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Minimal JSON DOM used to parse external manifests without adding a
// web runtime or a binary dependency to the launcher.
class JsonValue
{
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;

    JsonValue();
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    // Integers are stored as numbers; without these an int would be ambiguous.
    explicit JsonValue(int value);
    explicit JsonValue(long long value);
    explicit JsonValue(unsigned long long value);
    explicit JsonValue(std::string value);
    // A string literal must not decay to bool.
    explicit JsonValue(const char* value);
    explicit JsonValue(Array value);
    explicit JsonValue(Object value);

    [[nodiscard]] static std::optional<JsonValue> parse(
        std::string_view json, std::string* error = nullptr);
    // JSON text that parse() reads back. `indent` > 0 spreads objects and arrays over lines.
    [[nodiscard]] std::string serialize(int indent = 0) const;

    [[nodiscard]] const JsonValue* find(std::string_view key) const;
    [[nodiscard]] const std::string* stringValue() const;
    [[nodiscard]] const Array* arrayValue() const;
    [[nodiscard]] const Object* objectValue() const;
    [[nodiscard]] bool boolValue(bool fallback = false) const;
    [[nodiscard]] double numberValue(double fallback = 0) const;

private:
    using Storage = std::variant<std::monostate, bool, double, std::string, Array, Object>;
    Storage value_;
};
