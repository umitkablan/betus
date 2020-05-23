#pragma once

#include <boost/uuid/uuid_generators.hpp>
#include <boost/beast.hpp>

#include <ios>
#include <limits>
#include <mutex>
#include <fstream>
#include <unordered_set>
#include <string>

namespace tus
{

class FilesManager;

class TmpFilesResource
{
    friend class FilesManager;

    FilesManager& files_man_;
    bool persisted_;
    const std::string uuid_;

    std::ofstream md_ostr_;
    std::ofstream dt_ostr_;

    // Assure only FilesManager get it created
    TmpFilesResource(FilesManager& files_man, const std::string& uuid);

public:
    ~TmpFilesResource() noexcept;

    std::errc Initialize(size_t totlen, const std::string_view& md_comment = "");

    const std::string& Uuid() const { return uuid_; }
};

struct Metadata
{
    std::streamoff offset;
    size_t length;
    std::string comment;
};

class FileResource
{
    friend class FilesManager;

    FilesManager& files_man_;

    const std::string& uuid_;
    mutable std::fstream fstream_dt_;
    mutable std::fstream fstream_md_;
    bool delete_mark_;
    bool do_release_mark_;

    bool updateOffsetMetadata();

    // Make sure FileResource is only acquired from owner FilesManager
    FileResource(FilesManager& fm, const std::string& uuid);

public:
    ~FileResource() noexcept;
    FileResource(const FileResource&) = delete;
    FileResource(FileResource&&);

    bool IsOpen() const { return fstream_dt_.is_open() && fstream_md_.is_open(); }

    Metadata GetMetadata() const;
    std::string ChecksumSha1Hex(std::ifstream::pos_type begpos = 0, std::streamoff count = 0) const;

    size_t Write(std::streamoff offset_sz, const boost::beast::multi_buffer& body);
    bool Write(const std::string_view& data)
    {
        return !!fstream_dt_.write(data.data(), data.size());
    }

    void Delete() noexcept { delete_mark_ = true; }
    void Commit();
};

class FilesManager
{
    friend class TmpFilesResource;
    friend class FileResource;

    std::string dirpath_;
    mutable std::mutex fname_mtx_;
    std::unordered_set<std::string> all_fnames_;
    std::unordered_set<std::string> inuse_fnames_;
    boost::uuids::random_generator random_uuid_generator_;

public:
    static const std::string METADATA_FNAME_SUFFIX;

    FilesManager(const std::string& dirpath) : dirpath_(dirpath) {}

    TmpFilesResource NewTmpFilesResource();
    void Persist(TmpFilesResource& tmpres);

    std::pair<std::errc, FileResource>
        GetFileResource(const std::string& uuid);

    size_t Size() const { return all_fnames_.size(); }
    size_t RmAllFiles();

private:
    std::errc release(FileResource& fres) noexcept;

    std::string newUniqueFileName();
    std::string makeFPath(const std::string_view& sv) const;

    void deleteFiles(const std::string& uuid) noexcept;
    void erase(const std::string& uuid, bool delete_files) noexcept;
};

} // namespace tus
