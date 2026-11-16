#include "zip_archive.hpp"
#include "atomic_file.hpp"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include <algorithm>
#include <array>
#include <fstream>

namespace zip_archive {
namespace {
size_t writeStream(void* opaque, mz_uint64 offset, const void* data, size_t size) {
    auto& stream = *static_cast<std::fstream*>(opaque);
    stream.seekp(static_cast<std::streamoff>(offset));
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return stream ? size : 0;
}
size_t readStream(void* opaque, mz_uint64 offset, void* data, size_t size) {
    auto& stream = *static_cast<std::ifstream*>(opaque);
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return static_cast<size_t>(stream.gcount());
}
}

bool safeName(std::string_view name) {
    if (name.empty() || name.size() > 1024 || name.front() == '/' || name.back() == '/') return false;
    size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find('/', start), name.size());
        const auto part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        for (const char c : part)
            if (static_cast<unsigned char>(c) < 32 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        start = end + 1;
    }
    return true;
}

bool storedType(const std::filesystem::path& file) {
    auto extension = file.extension().wstring();
    for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
    for (const auto* known : {L".jpg", L".jpeg", L".png", L".webp", L".gif", L".img", L".mp4", L".webm", L".zip", L".7z", L".mp3", L".ogg", L".flac", L".dat"})
        if (extension == known) return true;
    return false;
}

struct Writer::State {
    mz_zip_archive zip{};
    std::fstream stream;
    bool open = false;
    uint64_t written = 0;
};
Writer::Writer() : state_(std::make_unique<State>()) {}
Writer::~Writer() { if (state_->open) mz_zip_writer_end(&state_->zip); }

bool Writer::open(const std::filesystem::path& file) {
    state_->stream.open(file, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!state_->stream) return false;
    state_->zip.m_pWrite = writeStream;
    state_->zip.m_pIO_opaque = &state_->stream;
    state_->open = mz_zip_writer_init_v2(&state_->zip, 0, 0);
    return state_->open;
}
bool Writer::add(std::string_view name, std::string_view bytes, bool store) {
    if (!state_->open || !safeName(name)) return false;
    const std::string entry(name);
    const bool ok = mz_zip_writer_add_mem_ex(&state_->zip, entry.c_str(), bytes.data(), bytes.size(), nullptr, 0,
                                             store ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL, 0, 0);
    if (ok) state_->written += bytes.size();
    return ok;
}
bool Writer::addFile(std::string_view name, const std::filesystem::path& source, bool store) {
    if (!state_->open || !safeName(name)) return false;
    std::ifstream input(source, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0) return false;
    const auto size = static_cast<uint64_t>(input.tellg());
    const std::string entry(name);
    const bool ok = mz_zip_writer_add_read_buf_callback(&state_->zip, entry.c_str(), readStream, &input, size, nullptr, nullptr, 0,
                                                        store ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL, nullptr, 0, nullptr, 0);
    if (ok) state_->written += size;
    return ok;
}
bool Writer::finish() {
    if (!state_->open) return false;
    const bool finalized = mz_zip_writer_finalize_archive(&state_->zip);
    mz_zip_writer_end(&state_->zip); state_->open = false;
    state_->stream.flush();
    const bool good = finalized && state_->stream.good();
    state_->stream.close();
    return good;
}
uint64_t Writer::written() const { return state_->written; }

struct Reader::State {
    mutable mz_zip_archive zip{};
    mutable std::ifstream stream;
    std::vector<Entry> entries;
    bool open = false;
};
Reader::Reader() : state_(std::make_unique<State>()) {}
Reader::~Reader() { if (state_->open) mz_zip_reader_end(&state_->zip); }

bool Reader::open(const std::filesystem::path& file) {
    state_->stream.open(file, std::ios::binary | std::ios::ate);
    if (!state_->stream || state_->stream.tellg() <= 0) return false;
    const auto size = static_cast<uint64_t>(state_->stream.tellg());
    state_->zip.m_pRead = readStream;
    state_->zip.m_pIO_opaque = &state_->stream;
    if (!mz_zip_reader_init(&state_->zip, size, 0)) return false;
    state_->open = true;
    const auto count = mz_zip_reader_get_num_files(&state_->zip);
    if (count > 400000) return false;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&state_->zip, i, &stat)) return false;
        state_->entries.push_back({stat.m_filename, stat.m_uncomp_size, static_cast<bool>(stat.m_is_directory)});
    }
    return true;
}
const std::vector<Entry>& Reader::entries() const { return state_->entries; }
int Reader::find(std::string_view name) const {
    for (size_t i = 0; i < state_->entries.size(); ++i) if (state_->entries[i].name == name) return static_cast<int>(i);
    return -1;
}
std::optional<std::string> Reader::read(std::string_view name, uint64_t limit) const {
    const int index = find(name);
    if (index < 0 || state_->entries[index].size > limit) return std::nullopt;
    std::string bytes(static_cast<size_t>(state_->entries[index].size), '\0');
    if (!mz_zip_reader_extract_to_mem(&state_->zip, static_cast<mz_uint>(index), bytes.data(), bytes.size(), 0)) return std::nullopt;
    return bytes;
}
bool Reader::extract(int index, const std::filesystem::path& target) const {
    if (index < 0 || index >= static_cast<int>(state_->entries.size()) || state_->entries[index].folder) return false;
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    const std::filesystem::path temporary = target.wstring() + L".tmp." + std::to_wstring(atomic_file::detail::processId()) + L".zip";
    bool ok = false;
    {
        std::fstream output(temporary, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
        ok = output && mz_zip_reader_extract_to_callback(&state_->zip, static_cast<mz_uint>(index), writeStream, &output, 0);
        output.flush(); ok = ok && output.good();
    }
    if (ok && atomic_file::replace(temporary, target)) return true;
    std::filesystem::remove(temporary, ec);
    return false;
}
}
