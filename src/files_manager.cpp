#include "include/files_manager.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/sha1.hpp>

#include <fstream>
#include <ios>
#include <iostream>
#include <string>

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
    if (persisted_) return;

    auto delete_md = md_ostr_.is_open();
    bool delete_dt = dt_ostr_.is_open();
    md_ostr_.close();
    dt_ostr_.close();
    files_man_.Delete(uuid_, delete_md, delete_dt);
}

std::ofstream& TmpFilesResource::MetadataFstream(size_t length, const std::string_view& sv)
{
    assert(md_fpath_.empty());

    md_fpath_ = files_man_.MakeFPath(uuid_ + FilesManager::METADATA_FNAME_SUFFIX);
    md_ostr_.open(md_fpath_);
    std::streamoff sz = 0;
    md_ostr_.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    md_ostr_.write(reinterpret_cast<const char*>(&length), sizeof(length));
    md_ostr_ << std::endl;
    if (!sv.empty() && md_ostr_.good()) md_ostr_ << sv << std::endl;
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

Metadata FilesManager::GetMetadata(const std::string& uuid) const
{
    Metadata ret{ -1, 0, ""};

    std::ifstream md_istr(MakeFPath(uuid + METADATA_FNAME_SUFFIX));
    if (!md_istr.is_open())
        return ret;
    md_istr.seekg(0, std::ios_base::beg);

    md_istr.read(reinterpret_cast<char*>(&ret.offset), sizeof(ret.offset));
    if (md_istr.bad())
        ret.offset = -1;
    md_istr.read(reinterpret_cast<char*>(&ret.length), sizeof(ret.length));
    if (md_istr.bad())
        ret.length = 0;
    std::getline(md_istr, ret.comment);
    assert(ret.comment.empty());
    std::getline(md_istr, ret.comment);

    return ret;
}

std::string FilesManager::ChecksumSha1Hex(const std::string &uuid,
                                          std::ifstream::pos_type begpos, std::streamoff count) const
{
    using boost::uuids::detail::sha1;
    using boost::algorithm::hex;

    std::string ret;
    std::ifstream dt_istr(MakeFPath(uuid));

    if (!dt_istr.is_open())
        return ret;
    const auto filesz = [&dt_istr]() {
        dt_istr.seekg(0, std::ios_base::end);
        return dt_istr.tellg();
    }();
    if (begpos >= filesz)
        return ret;
    if (count == 0) count = filesz - begpos;
    if (count > (filesz - begpos))
        return ret;
    dt_istr.seekg(begpos, std::ios_base::beg);

    sha1 gen;
    char datblock[2048];
    for (; count > 0 && !dt_istr.eof() && dt_istr.good();
            count -= dt_istr.gcount())
    {
        dt_istr.readsome(datblock,
                         std::min(count, static_cast<std::ifstream::off_type>(2048)));
        gen.process_block(datblock, datblock + dt_istr.gcount());
    }

    ret.reserve(42);
    sha1::digest_type dig;
    gen.get_digest(dig);
    hex(std::begin(dig), std::end(dig), std::back_inserter(ret));

    return ret;
}

size_t FilesManager::Write(const std::string& uuid, std::streamoff offset_sz,
                           const boost::beast::multi_buffer& body)
{
    std::fstream dt_ostr(MakeFPath(uuid));
    if (!dt_ostr.is_open())
        return 0;
    dt_ostr.seekp(offset_sz, std::ios_base::beg);
    assert(offset_sz == dt_ostr.tellp());

    const auto& buf = body.cdata();
    size_t ret = 0;
    for (const auto& constbuf : buf)
    {
        auto datp = static_cast<const char*>(constbuf.data());
        ret += constbuf.size();
        if (!dt_ostr.write(datp, constbuf.size()))
            return 0;
    }
    return ret;
}

bool FilesManager::UpdateOffsetMetadata(const std::string& uuid, std::streamoff newoff)
{
    std::fstream md_ostr(MakeFPath(uuid + METADATA_FNAME_SUFFIX));
    if (!md_ostr.is_open())
        return false;
    return !!md_ostr.write(reinterpret_cast<const char*>(&newoff), sizeof(newoff));
}

bool FilesManager::Delete(const std::string& uuid, bool delete_md, bool delete_dt) noexcept
{
    auto rm_and_log = [](const std::string & fpath)
    {
        auto res = ::remove(fpath.c_str());
        if (res)
            std::cerr << "remove " << fpath << " failed: " << res << std::endl;
    };

    if (delete_dt)
        rm_and_log(MakeFPath(uuid));
    if (delete_md)
        rm_and_log(MakeFPath(uuid + METADATA_FNAME_SUFFIX));
    return rmUniqueFileName(uuid);
}

size_t FilesManager::RmAllFiles()
{
    auto ret = all_fnames_.size();
    for (const auto& fuuid : all_fnames_)
        Delete(fuuid);
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
