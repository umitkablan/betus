#include "include/files_manager.hpp"

#include <boost/uuid/uuid_io.hpp>

#include <fstream>
#include <ios>
#include <iostream>

namespace tus
{

namespace http = boost::beast::http;

const std::string FilesManager::METADATA_FNAME_SUFFIX = ".mdata";

TmpFilesResource::TmpFilesResource(FilesManager& files_man, const std::string& uuid)
    : files_man_(files_man), persisted_(false), uuid_(uuid)
{
}

TmpFilesResource::~TmpFilesResource() noexcept
{
    if (persisted_)
        return;

    auto rmfun = [](std::ofstream & ostr, const std::string & fpath)
    {
        if (ostr.is_open())
        {
            ostr.close();
            auto res = ::remove(fpath.c_str());
            if (res)
                std::cerr << "remove " << fpath << " failed: " << res << std::endl;
        }
    };
    rmfun(md_ostr_, md_fpath_);
    rmfun(dt_ostr_, dt_fpath_);
    files_man_.rmUniqueFileName(uuid_);
}

std::ofstream& TmpFilesResource::MetadataFstream()
{
    assert(md_fpath_.empty());

    md_fpath_ = files_man_.MakeFPath(uuid_ + FilesManager::METADATA_FNAME_SUFFIX);
    md_ostr_.open(md_fpath_);
    std::streamoff sz = 0;
    md_ostr_.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    md_ostr_ << std::endl;
    return md_ostr_;
}

std::ofstream& TmpFilesResource::DataFstream(size_t reserve_sz)
{
    assert(dt_fpath_.empty());

    dt_fpath_ = files_man_.MakeFPath(uuid_);
    dt_ostr_.open(dt_fpath_);
    if (dt_ostr_.is_open())
        return dt_ostr_;
    dt_ostr_.seekp(reserve_sz, std::ios_base::beg);
    dt_ostr_.seekp(0, std::ios_base::beg);
    return dt_ostr_;
}

TmpFilesResource FilesManager::NewTmpFilesResource()
{
    return TmpFilesResource(*this, newUniqueFileName());
}

void FilesManager::Persist(TmpFilesResource& tmpres)
{
    tmpres.persisted_ = true;
}

bool FilesManager::HasFile(const std::string& uuid) const
{
    std::lock_guard lock(fname_mtx_);

    return all_fnames_.count(uuid) > 0;
}

size_t FilesManager::Write(const std::string& uuid, std::streamoff offset_sz, const boost::beast::multi_buffer& body)
{
    std::fstream dt_ostr(MakeFPath(uuid));
    std::fstream md_ostr(MakeFPath(uuid + METADATA_FNAME_SUFFIX));
    if (!dt_ostr.is_open() || !md_ostr.is_open())
        return 0;
    dt_ostr.seekp(offset_sz, std::ios_base::beg);
    assert(offset_sz == dt_ostr.tellp());

    md_ostr.seekp(0, std::ios_base::end);
    const auto& buf = body.cdata();
    size_t ret = 0;
    for (const auto& constbuf : buf)
    {
        auto datp = static_cast<const char*>(constbuf.data());
        ret += constbuf.size();
        if (!dt_ostr.write(datp, constbuf.size()))
            return 0;

        std::streamoff sz = dt_ostr.tellp();
        md_ostr.seekp(0, std::ios_base::beg);
        md_ostr.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    }
    return ret;
}

std::string FilesManager::newUniqueFileName()
{
    std::lock_guard lock(fname_mtx_);

    std::string ret;
    auto it = all_fnames_.cbegin();
    do
    {
        boost::uuids::uuid uuid = random_uuid_generator_();
        ret = to_string(uuid);
        it = all_fnames_.find(ret);
    }
    while (it != all_fnames_.cend());
    all_fnames_.emplace(ret);
    return ret;
}

bool FilesManager::rmUniqueFileName(const std::string& fname)
{
    std::lock_guard lock(fname_mtx_);
    return all_fnames_.erase(fname) > 0;
}

} // namespace tus
