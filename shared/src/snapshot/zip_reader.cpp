#include "pch.h"
#include <insti/snapshot/zip_reader.h>
#include <aclapi.h>
#include <sddl.h>

namespace insti
{

namespace
{

/// Set permissive ACL on a file (Everyone: Full Control)
bool set_permissive_acl(const std::wstring& path)
{
    // SDDL: D:(A;;FA;;;WD) = DACL with Allow Full Access to Everyone (World)
    PSECURITY_DESCRIPTOR pSD = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;FA;;;WD)", SDDL_REVISION_1, &pSD, nullptr))
    {
        return false;
    }

    BOOL daclPresent = FALSE, daclDefaulted = FALSE;
    PACL pDacl = nullptr;
    if (!GetSecurityDescriptorDacl(pSD, &daclPresent, &pDacl, &daclDefaulted))
    {
        LocalFree(pSD);
        return false;
    }

    DWORD result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, pDacl, nullptr);

    LocalFree(pSD);
    return result == ERROR_SUCCESS;
}

} // anonymous namespace

ZipSnapshotReader::ZipSnapshotReader()
    : m_zip{new mz_zip_archive{}}
    , m_open{false}
{
}

ZipSnapshotReader::~ZipSnapshotReader()
{
    close();
    delete static_cast<mz_zip_archive*>(m_zip);
}

bool ZipSnapshotReader::open(std::string_view path)
{
    close();

    auto* zip = static_cast<mz_zip_archive*>(m_zip);
    memset(zip, 0, sizeof(mz_zip_archive));

    std::string path_str{path};
    if (!mz_zip_reader_init_file(zip, path_str.c_str(), 0))
    {
        spdlog::error("Failed to open zip: {}", path);
        return false;
    }

    m_open = true;
    build_path_cache();  // Build cache immediately
    return true;
}

void ZipSnapshotReader::close()
{
    if (m_open)
    {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(m_zip));
        m_open = false;
    }
}

std::vector<std::string> ZipSnapshotReader::get_all_paths() const
{
    std::vector<std::string> result;

    if (!m_open)
        return result;

    auto* zip = static_cast<mz_zip_archive*>(m_zip);
    mz_uint count = mz_zip_reader_get_num_files(zip);
    result.reserve(count);

    for (mz_uint i = 0; i < count; ++i)
    {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(zip, i, &stat))
            result.push_back(stat.m_filename);
    }

    return result;
}

std::vector<uint8_t> ZipSnapshotReader::read_binary(std::string_view path) const
{
    if (!m_open)
        return {};

    auto* zip = static_cast<mz_zip_archive*>(m_zip);

    std::string path_str{path};
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(zip, path_str.c_str(), &size, 0);
    if (!data)
        return {};

    std::vector<uint8_t> result(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    mz_free(data);
    return result;
}

bool ZipSnapshotReader::extract_to_file(std::string_view archive_path, std::string_view dest_path) const
{
    if (!m_open)
        return false;

    // Ensure parent directory exists
    std::filesystem::path dest{dest_path};
    if (dest.has_parent_path())
        std::filesystem::create_directories(dest.parent_path());

    std::string archive_str{archive_path};
    std::string dest_str{dest_path};
    bool ok = mz_zip_reader_extract_file_to_file(
        static_cast<mz_zip_archive*>(m_zip),
        archive_str.c_str(),
        dest_str.c_str(),
        0);

    if (ok)
    {
        // Set permissive ACL so non-admin users can access the files
        set_permissive_acl(dest.wstring());
    }

    return ok;
}

} // namespace insti
