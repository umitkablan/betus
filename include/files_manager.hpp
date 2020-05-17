#pragma once

#include <boost/uuid/uuid_generators.hpp>
#include <boost/beast.hpp>

#include <ios>
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

    std::string md_fpath_;
    std::string dt_fpath_;
    std::ofstream md_ostr_;
    std::ofstream dt_ostr_;

public:
    ~TmpFilesResource() noexcept;

    const std::string& MetadataPath() const { return md_fpath_; }
    const std::string& DataPath()     const { return dt_fpath_; }
    const std::string& Uuid()         const { return uuid_; }

    std::ofstream& MetadataFstream(size_t length = 0, const std::string_view& sv = std::string_view());
    std::ofstream& DataFstream(size_t reserve_sz);

private:
    TmpFilesResource(FilesManager& files_man, const std::string& uuid);
};

struct Metadata
{
    std::streamoff offset;
    size_t length;
    std::string comment;
};

class FilesManager
{
    friend class TmpFilesResource;

    std::string dirpath_;
    mutable std::mutex fname_mtx_;
    std::unordered_set<std::string> all_fnames_;
    boost::uuids::random_generator random_uuid_generator_;

public:
    static const std::string METADATA_FNAME_SUFFIX;

    FilesManager(const std::string& dirpath) : dirpath_(dirpath) {}

    TmpFilesResource NewTmpFilesResource();
    void Persist(TmpFilesResource& tmpres);

    size_t Size() const { return all_fnames_.size(); }
    bool HasFile(const std::string& uuid) const;
    Metadata GetMetadata(const std::string& uuid) const;
    size_t Write(const std::string& uuid,  std::streamoff offset_sz, const boost::beast::multi_buffer& body);
    bool Delete(const std::string& uuid, bool delete_md = true, bool delete_dt = true) noexcept;

private:
    std::string MakeFPath(const std::string_view& sv) const
    {
        auto ret = dirpath_ + '/';
        ret += sv;
        return ret;
    }

    std::string newUniqueFileName();
    bool rmUniqueFileName(const std::string& fname);
};

} // namespace tus
