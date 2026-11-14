#include "services/ServerList.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    enum Type : std::uint8_t { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6, ByteArray = 7, String = 8,
                               List = 9, Compound = 10, IntArray = 11, LongArray = 12 };

    // One NBT tag. Numbers and arrays keep their exact bytes, so everything the game wrote
    // is written back unchanged; only strings, lists and compounds are understood.
    struct Tag
    {
        std::uint8_t type = End;
        std::string name;
        std::string raw;            // fixed-size numbers and arrays, as stored
        std::string text;           // String
        std::uint8_t elementType = End;
        std::vector<Tag> children;  // List elements or Compound entries
    };

    class Reader
    {
    public:
        explicit Reader(std::string_view data) : data_(data) {}
        bool done() const { return position_ == data_.size(); }

        bool named(Tag& tag)
        {
            if (!byte(tag.type)) return false;
            if (tag.type == End) return true;
            return string(tag.name) && payload(tag, 0);
        }

    private:
        bool take(std::size_t count, std::string_view& out)
        {
            if (data_.size() - position_ < count) return false;
            out = data_.substr(position_, count);
            position_ += count;
            return true;
        }
        bool byte(std::uint8_t& value)
        {
            std::string_view bytes;
            if (!take(1, bytes)) return false;
            value = static_cast<std::uint8_t>(bytes[0]);
            return true;
        }
        bool integer(std::int32_t& value)
        {
            std::string_view bytes;
            if (!take(4, bytes)) return false;
            value = static_cast<std::int32_t>((static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) << 24) |
                                              (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 16) |
                                              (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 8) |
                                              static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[3])));
            return true;
        }
        bool string(std::string& value)
        {
            std::string_view length;
            if (!take(2, length)) return false;
            const std::size_t size = (static_cast<std::size_t>(static_cast<std::uint8_t>(length[0])) << 8) | static_cast<std::uint8_t>(length[1]);
            std::string_view bytes;
            if (!take(size, bytes)) return false;
            value.assign(bytes);
            return true;
        }
        bool array(Tag& tag, std::size_t width)
        {
            const auto start = position_;
            std::int32_t count = 0;
            if (!integer(count) || count < 0) return false;
            std::string_view bytes;
            if (!take(static_cast<std::size_t>(count) * width, bytes)) return false;
            tag.raw.assign(data_.substr(start, position_ - start));
            return true;
        }
        bool payload(Tag& tag, int depth)
        {
            if (depth > 64) return false;
            std::string_view bytes;
            switch (tag.type) {
            case Byte: if (!take(1, bytes)) return false; tag.raw.assign(bytes); return true;
            case Short: if (!take(2, bytes)) return false; tag.raw.assign(bytes); return true;
            case Int: case Float: if (!take(4, bytes)) return false; tag.raw.assign(bytes); return true;
            case Long: case Double: if (!take(8, bytes)) return false; tag.raw.assign(bytes); return true;
            case ByteArray: return array(tag, 1);
            case IntArray: return array(tag, 4);
            case LongArray: return array(tag, 8);
            case String: return string(tag.text);
            case List: {
                std::int32_t count = 0;
                if (!byte(tag.elementType) || !integer(count) || count < 0 || count > 100000) return false;
                for (std::int32_t i = 0; i < count; ++i) {
                    Tag element;
                    element.type = tag.elementType;
                    if (!payload(element, depth + 1)) return false;
                    tag.children.push_back(std::move(element));
                }
                return true;
            }
            case Compound:
                for (;;) {
                    Tag entry;
                    if (!byte(entry.type)) return false;
                    if (entry.type == End) return true;
                    if (!string(entry.name) || !payload(entry, depth + 1)) return false;
                    tag.children.push_back(std::move(entry));
                }
            default:
                return false;
            }
        }

        std::string_view data_;
        std::size_t position_ = 0;
    };

    void putString(std::string& out, const std::string& value)
    {
        out.push_back(static_cast<char>((value.size() >> 8) & 0xFF));
        out.push_back(static_cast<char>(value.size() & 0xFF));
        out += value;
    }

    void putPayload(std::string& out, const Tag& tag)
    {
        switch (tag.type) {
        case String: putString(out, tag.text); return;
        case List: {
            out.push_back(static_cast<char>(tag.elementType));
            const auto count = static_cast<std::uint32_t>(tag.children.size());
            for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>((count >> shift) & 0xFF));
            for (const auto& element : tag.children) putPayload(out, element);
            return;
        }
        case Compound:
            for (const auto& entry : tag.children) {
                out.push_back(static_cast<char>(entry.type));
                putString(out, entry.name);
                putPayload(out, entry);
            }
            out.push_back(static_cast<char>(End));
            return;
        default:
            out += tag.raw;
        }
    }

    Tag* child(Tag& compound, std::string_view name)
    {
        for (auto& entry : compound.children)
            if (entry.name == name) return &entry;
        return nullptr;
    }

    Tag stringTag(std::string name, std::string value)
    {
        Tag tag;
        tag.type = String;
        tag.name = std::move(name);
        tag.text = std::move(value);
        return tag;
    }

    std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    // "host", "host:port" or "[v6]:port" → the host, lowercase.
    std::string hostOf(const std::string& address)
    {
        auto host = address;
        if (!host.empty() && host.front() == '[') host = host.substr(1, host.find(']') == std::string::npos ? std::string::npos : host.find(']') - 1);
        else if (const auto colon = host.find(':'); colon != std::string::npos && host.find(':', colon + 1) == std::string::npos) host.resize(colon);
        return lower(host);
    }

    std::optional<Tag> parse(std::string_view content)
    {
        Tag root;
        if (content.empty()) {
            root.type = Compound;
            return root;
        }
        Reader reader(content);
        if (!reader.named(root) || root.type != Compound || !reader.done()) return std::nullopt;
        return root;
    }
}

std::optional<std::string> ServerList::merge(std::string_view current, std::string_view name, std::string_view address,
                                             std::string_view retiredDomain)
{
    auto root = parse(current);
    if (!root) return std::nullopt;
    Tag* servers = child(*root, "servers");
    if (!servers) {
        Tag list;
        list.type = List;
        list.name = "servers";
        list.elementType = Compound;
        root->children.push_back(std::move(list));
        servers = &root->children.back();
    }
    if (servers->type != List || (servers->elementType != Compound && !servers->children.empty())) return std::nullopt;
    servers->elementType = Compound;

    const std::string target = lower(std::string(address)), retired = lower(std::string(retiredDomain));
    bool present = false;
    for (auto& entry : servers->children) {
        Tag* ip = child(entry, "ip");
        if (!ip || ip->type != String) continue;
        const auto host = hostOf(ip->text);
        // The old address stops working: the player's own entry follows the group to the new one.
        if (!retired.empty() && (host == retired || (host.size() > retired.size() && host.ends_with("." + retired)))) ip->text = std::string(address);
        present |= hostOf(ip->text) == target;
    }
    if (!present) {
        Tag entry;
        entry.type = Compound;
        entry.children.push_back(stringTag("name", std::string(name)));
        entry.children.push_back(stringTag("ip", std::string(address)));
        servers->children.insert(servers->children.begin(), std::move(entry));
    }
    std::string out;
    out.push_back(static_cast<char>(Compound));
    putString(out, root->name);
    putPayload(out, *root);
    return out;
}

std::optional<std::string> ServerList::addresses(std::string_view content)
{
    auto root = parse(content);
    if (!root) return std::nullopt;
    std::string list;
    if (Tag* servers = child(*root, "servers"))
        for (auto& entry : servers->children)
            if (Tag* ip = child(entry, "ip"); ip && ip->type == String) list += ip->text + "\n";
    return list;
}

bool ServerList::ensure(const std::filesystem::path& gameDirectory, std::string_view name, std::string_view address,
                        std::string_view retiredDomain)
{
    const auto file = gameDirectory / L"servers.dat";
    std::string current;
    {
        std::ifstream input(file, std::ios::binary);
        current.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    const auto merged = merge(current, name, address, retiredDomain);
    if (!merged) return false;  // a file this code cannot read is left alone
    if (*merged == current) return true;
    std::error_code ignored;
    std::filesystem::create_directories(gameDirectory, ignored);
    auto partial = file;
    partial += L".part";
    {
        std::ofstream output(partial, std::ios::binary | std::ios::trunc);
        output.write(merged->data(), static_cast<std::streamsize>(merged->size()));
        if (!output) return false;
    }
    return MoveFileExW(partial.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
