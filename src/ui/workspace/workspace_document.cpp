#include "workspace_document.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "figure_serializer.hpp"

namespace spectra
{

namespace
{

constexpr char MAGIC[4] = {'S', 'P', 'W', 'S'};

void put_u32(std::string& out, uint32_t v)
{
    char b[4];
    std::memcpy(b, &v, 4);
    out.append(b, 4);
}

bool get_u32(std::string_view bytes, size_t& pos, uint32_t& v)
{
    if (bytes.size() < pos + 4)
        return false;
    std::memcpy(&v, bytes.data() + pos, 4);
    pos += 4;
    return true;
}

bool get_blob(std::string_view bytes, size_t& pos, std::string& out)
{
    uint32_t len = 0;
    if (!get_u32(bytes, pos, len) || bytes.size() - pos < len)
        return false;
    out.assign(bytes.data() + pos, len);
    pos += len;
    return true;
}

}   // namespace

std::string WorkspaceDocument::encode(const WorkspaceDocumentContents& contents)
{
    size_t total = 16 + contents.workspace_json.size();
    for (const auto& b : contents.figure_blobs)
        total += 4 + b.size();

    std::string out;
    out.reserve(total);
    out.append(MAGIC, 4);
    put_u32(out, VERSION);
    put_u32(out, static_cast<uint32_t>(contents.workspace_json.size()));
    out += contents.workspace_json;
    put_u32(out, static_cast<uint32_t>(contents.figure_blobs.size()));
    for (const auto& b : contents.figure_blobs)
    {
        put_u32(out, static_cast<uint32_t>(b.size()));
        out += b;
    }
    return out;
}

bool WorkspaceDocument::decode(std::string_view bytes, WorkspaceDocumentContents& out)
{
    if (!has_magic(bytes))
        return false;
    size_t   pos     = 4;
    uint32_t version = 0;
    if (!get_u32(bytes, pos, version) || version == 0 || version > VERSION)
        return false;

    WorkspaceDocumentContents c;
    if (!get_blob(bytes, pos, c.workspace_json))
        return false;
    uint32_t count = 0;
    if (!get_u32(bytes, pos, count))
        return false;
    // Each figure needs at least its length field.
    if (count > (bytes.size() - pos) / 4)
        return false;
    c.figure_blobs.resize(count);
    for (auto& blob : c.figure_blobs)
    {
        if (!get_blob(bytes, pos, blob))
            return false;
    }
    out = std::move(c);
    return true;
}

bool WorkspaceDocument::write_file(const std::string& path,
                                   std::string_view   bytes,
                                   std::string*       error)
{
    namespace fs = std::filesystem;
    auto fail    = [error](std::string why)
    {
        if (error)
            *error = std::move(why);
        return false;
    };
    if (path.empty())
        return fail("no file name");

    std::error_code ec;
    const fs::path  target(path);
    if (target.has_parent_path() && !fs::exists(target.parent_path(), ec))
        return fail("folder " + target.parent_path().string() + " does not exist");

    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return fail("cannot write to " + path + " (permission denied?)");
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if (!f.good())
        {
            f.close();
            fs::remove(tmp, ec);
            return fail("write error (disk full?)");
        }
    }
    fs::rename(tmp, target, ec);
    if (ec)
    {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return fail(ec.message());
    }
    return true;
}

bool WorkspaceDocument::read_file(const std::string& path, std::string& bytes)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

bool WorkspaceDocument::has_magic(std::string_view head)
{
    return head.size() >= 4 && std::memcmp(head.data(), MAGIC, 4) == 0;
}

SpectraFileKind WorkspaceDocument::sniff(std::string_view head)
{
    if (has_magic(head))
        return SpectraFileKind::WorkspaceDocument;
    if (FigureSerializer::has_figure_magic(head))
        return SpectraFileKind::Figure;
    for (char c : head)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        return c == '{' ? SpectraFileKind::Json : SpectraFileKind::Unknown;
    }
    return SpectraFileKind::Unknown;
}

SpectraFileKind WorkspaceDocument::sniff_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return SpectraFileKind::Missing;
    char buf[64] = {};
    f.read(buf, sizeof(buf));
    const auto n = static_cast<size_t>(f.gcount());
    return sniff(std::string_view(buf, n));
}

}   // namespace spectra
