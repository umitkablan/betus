#include "include/files_manager.hpp"

#include <boost/algorithm/hex.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/sha1.hpp>

#include <fstream>
#include <ios>
#include <iostream>
#include <string>
#include <system_error>

namespace tus
{

namespace http = boost::beast::http;

const std::string FilesManager::METADATA_FNAME_SUFFIX = ".mdata";
static const std::string Empty_String;

TmpFilesResource::TmpFilesResource(FilesManager& files_man, const std::string& uuid)
    : files_man_(files_man), uuid_(uuid), persisted_(false), do_erase_(true)
{
    md_ostr_.open(files_man_.makeFPath(uuid_ + FilesManager::METADATA_FNAME_SUFFIX));
    dt_ostr_.open(files_man_.makeFPath(uuid_));
}

TmpFilesResource::~TmpFilesResource() noexcept
{
    md_ostr_.close();
    dt_ostr_.close();
    if (do_erase_)
        files_man_.erase(uuid_, !persisted_);
}

std::errc TmpFilesResource::Initialize(size_t totlen, const std::string_view& md_comment)
{
    if (!dt_ostr_.good() || !md_ostr_.good())
        return std::errc::bad_file_descriptor;

    if (dt_ostr_)
    {
        dt_ostr_.seekp(totlen, std::ios_base::beg);
        dt_ostr_.seekp(0, std::ios_base::beg);
    }
    if (md_ostr_)
    {
        decltype(Metadata::offset) offset = 0;
        md_ostr_.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
        md_ostr_.write(reinterpret_cast<const char*>(&totlen), sizeof(totlen));
        md_ostr_ << std::endl;
    }
    if (!md_comment.empty() && md_ostr_.good())
        md_ostr_ << md_comment << '\n';
    md_ostr_ << '\n';

    if (!md_ostr_ || !dt_ostr_)
        return std::errc::bad_file_descriptor;
    else return static_cast<std::errc>(0);
}

TmpFilesResource FilesManager::NewTmpFilesResource()
{
    return TmpFilesResource(*this, newUniqueFileName());
}

void FilesManager::Persist(TmpFilesResource& tmpres)
{
    tmpres.persisted_ = true;
}

std::pair<std::errc, FileResource>
FilesManager::GetFileResource(const std::string& uuid)
{
    auto it = all_fnames_.begin();
    bool inserted = false;
    {
        std::lock_guard lock(fname_mtx_);

        it = all_fnames_.find(uuid);
        if (it != all_fnames_.end())
        {
            auto it1 = inuse_fnames_.find(uuid);
            if (it1 == inuse_fnames_.end())
            {
                inserted = true;
                inuse_fnames_.insert(uuid);
            }
        }
    }

    if (it == all_fnames_.end())
    {
        return std::pair<std::errc, FileResource>(
                   std::errc::no_such_file_or_directory,
                   FileResource(*this, Empty_String));
    }
    if (inserted)
    {
        return std::pair<std::errc, FileResource>(
                   static_cast<std::errc>(0),
                   FileResource(*this, uuid));
    }
    return std::pair<std::errc, FileResource>(
               std::errc::device_or_resource_busy,
               FileResource(*this, Empty_String));
}

size_t FilesManager::RmAllFiles()
{
    std::lock_guard lock(fname_mtx_);

    auto ret = all_fnames_.size();
    for (const auto& fuuid : all_fnames_)
        deleteFiles(fuuid);

    all_fnames_.clear();
    inuse_fnames_.clear();
    return ret;
}

std::errc FilesManager::release(FileResource& fres) noexcept
{
    std::lock_guard lock(fname_mtx_);

    auto it = all_fnames_.find(fres.uuid_);
    if (it == all_fnames_.end())
        return std::errc::no_such_file_or_directory;
    if (fres.delete_mark_)
        all_fnames_.erase(it);

    it = inuse_fnames_.find(fres.uuid_);
    if (it == inuse_fnames_.end())
        return std::errc::no_such_file_or_directory;
    inuse_fnames_.erase(it);

    return static_cast<std::errc>(0);
}

const std::string& FilesManager::newUniqueFileName()
{
    std::lock_guard lock(fname_mtx_);

    std::string uuidstr;
    auto it = all_fnames_.cbegin();
    do {
        boost::uuids::uuid uuid = random_uuid_generator_();
        uuidstr = to_string(uuid);
        it = all_fnames_.find(uuidstr);
    } while (it != all_fnames_.cend());

    inuse_fnames_.emplace(uuidstr);
    return *all_fnames_.emplace(uuidstr).first;
}

std::string FilesManager::makeFPath(const std::string_view& sv) const
{
    auto ret = dirpath_ + '/';
    ret += sv;
    return ret;
}

bool FilesManager::deleteFiles(const std::string& uuid) noexcept
{
    auto rm_or_log = [](const std::string & fpath) {
        auto res = ::remove(fpath.c_str());
        if (res)
            std::cerr << "remove " << fpath << " failed: " << res << std::endl;
        return res;
    };
    auto ret = rm_or_log(makeFPath(uuid));
    ret |= rm_or_log(makeFPath(uuid + FilesManager::METADATA_FNAME_SUFFIX));
    return !ret;
}

void FilesManager::erase(const std::string& uuid, bool delete_files) noexcept
{
    std::lock_guard lock(fname_mtx_);

    if (delete_files)
    {
        deleteFiles(uuid);
        all_fnames_.erase(uuid);
    }
    inuse_fnames_.erase(uuid);
}

FileResource::FileResource(FilesManager& fm, const std::string& uuid)
    : files_man_(fm), uuid_(uuid), delete_mark_(false), do_release_mark_(true)
{
    if (uuid_.empty()) return;
    fstream_dt_.open(files_man_.makeFPath(uuid_));
    fstream_md_.open(files_man_.makeFPath(uuid_ + FilesManager::METADATA_FNAME_SUFFIX));
}

FileResource::FileResource(TmpFilesResource&& tmpres)
    : files_man_(tmpres.files_man_), uuid_(tmpres.uuid_), delete_mark_(false), do_release_mark_(true)
{
    tmpres.dt_ostr_.close();
    tmpres.md_ostr_.close();
    tmpres.persisted_ = true;
    tmpres.do_erase_ = false;

    if (uuid_.empty()) return;
    fstream_dt_.open(files_man_.makeFPath(uuid_));
    fstream_md_.open(files_man_.makeFPath(uuid_ + FilesManager::METADATA_FNAME_SUFFIX));
}

FileResource::FileResource(FileResource&& o)
    : files_man_(o.files_man_), uuid_(o.uuid_),
      fstream_dt_(std::move(o.fstream_dt_)), fstream_md_(std::move(o.fstream_md_)),
      delete_mark_(o.delete_mark_), do_release_mark_(true)
{
    o.do_release_mark_ = false;
}

FileResource::~FileResource() noexcept
{
    fstream_dt_.close();
    fstream_md_.close();

    if (do_release_mark_ && !uuid_.empty())
        files_man_.release(*this);
}

Metadata FileResource::GetMetadata() const
{
    Metadata ret{ -1, 0, ""};

    if (!fstream_md_.is_open())
        return ret;
    fstream_md_.seekg(0, std::ios_base::beg);

    fstream_md_.read(reinterpret_cast<char*>(&ret.offset), sizeof(ret.offset));
    if (fstream_md_.bad())
        ret.offset = -1;
    fstream_md_.read(reinterpret_cast<char*>(&ret.length), sizeof(ret.length));
    if (fstream_md_.bad())
        ret.length = 0;
    std::getline(fstream_md_, ret.comment);
    assert(ret.comment.empty());
    std::getline(fstream_md_, ret.comment);

    return ret;
}

std::string FileResource::ChecksumSha1Hex(std::ifstream::pos_type begpos, std::streamoff count) const
{
    using boost::uuids::detail::sha1;
    using boost::algorithm::hex;

    std::string ret;

    if (!fstream_dt_.is_open())
        return ret;
    const auto filesz = [this]() {
        fstream_dt_.seekg(0, std::ios_base::end);
        return fstream_dt_.tellg();
    }();
    if (begpos >= filesz)
        return ret;
    if (count == 0) count = filesz - begpos;
    if (count > (filesz - begpos))
        return ret;
    fstream_dt_.seekg(begpos, std::ios_base::beg);

    sha1 gen;
    char datblock[2048];
    for (; count > 0 && !fstream_dt_.eof() && fstream_dt_.good();
            count -= fstream_dt_.gcount())
    {
        fstream_dt_.readsome(datblock,
                             std::min(count, static_cast<std::ifstream::off_type>(2048)));
        gen.process_block(datblock, datblock + fstream_dt_.gcount());
    }

    ret.reserve(42);
    sha1::digest_type dig;
    gen.get_digest(dig);
    hex(std::begin(dig), std::end(dig), std::back_inserter(ret));

    return ret;
}

size_t FileResource::Write(std::streamoff offset_sz, const boost::beast::multi_buffer& body)
{
    if (!fstream_dt_.is_open())
        return 0;
    fstream_dt_.seekp(offset_sz, std::ios_base::beg);
    assert(offset_sz == fstream_dt_.tellp());

    const auto& buf = body.cdata();
    size_t ret = 0;
    for (const auto& constbuf : buf)
    {
        auto datp = static_cast<const char*>(constbuf.data());
        ret += constbuf.size();
        if (!Write({datp, constbuf.size()}))
            return 0;
    }
    return ret;
}

bool FileResource::Commit()
{
    if (delete_mark_)
    {
        fstream_dt_.close();
        fstream_md_.close();
        return files_man_.deleteFiles(uuid_);
    }
    return updateOffsetMetadata();
}

bool FileResource::updateOffsetMetadata()
{
    if (!fstream_md_.is_open() || !fstream_dt_.is_open())
        return false;
    decltype(Metadata::offset) newoff = fstream_dt_.tellp();

    fstream_md_.seekp(0, std::ios_base::beg);
    return !!fstream_md_.write(reinterpret_cast<const char*>(&newoff), sizeof(newoff));
}
} // namespace tus
