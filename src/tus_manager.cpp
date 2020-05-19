#include "include/tus_manager.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>

#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>

#include <iostream>
#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>

namespace http = boost::beast::http;

namespace
{
template <typename NumType, typename T>
auto Parse_Number_From_Req(const http::request<http::dynamic_body>& req, const T& tag)
{
    auto it = req.find(tag);
    std::pair<bool, NumType> ret(it != req.cend(), static_cast<NumType>(0));
    if (!ret.first) return ret;
    auto [ptr, ec] = std::from_chars(it->value().begin(), it->value().end(), ret.second);
    if (std::make_error_code(ec)) ret.first = false;
    return ret;
}

template <typename T>
auto Parse_From_Req(const http::request<http::dynamic_body>& req, const T& tag)
{
    auto it = req.find(tag);
    std::pair<bool, std::string> ret(it != req.cend(), "");
    if (!ret.first) return ret;
    ret.second = it->value().to_string();
    return ret;
}

std::string Base64_To_Bin(const std::string_view &shastr)
{
    namespace ar_iters = boost::archive::iterators;
    using ItBinaryT = ar_iters::transform_width<
                        ar_iters::binary_from_base64<std::string::const_iterator>,
                        8, 6>;

    std::string input(std::begin(shastr), std::end(shastr));
    std::string output;
    output.reserve(24);

    size_t pad_chars(std::count(input.begin(), input.end(), '='));
    std::replace(input.begin(), input.end(), '=', 'A');
    try
    {
        std::copy(ItBinaryT(input.begin()), ItBinaryT(input.end()),
                  std::back_inserter(output));
    } catch (std::exception const &) {}
    output.resize(output.size() - pad_chars);
    return output;
}

bool CheckSum_Match(const std::string_view& hexstr, const std::string_view& b64_bin)
{
    const char* digits = "0123456789ABCDEF";
    assert(!(hexstr.size() % 2));

    if (2 * b64_bin.size() != hexstr.size())
        return false;
    for (size_t i = 0; i < hexstr.size(); i += 2)
    {
        unsigned char uc = b64_bin[i / 2];
        if (hexstr[i]   != digits[(uc & 0xf0) >> 4] ||
            hexstr[i+1] != digits[(uc & 0x0f)])
            return false;
    }
    return true;
}
}

namespace tus
{

const std::string TusManager::TAG_TUS_RESUMABLE   = "Tus-Resumable";
const std::string TusManager::TAG_TUS_VERSION     = "Tus-Version";
const std::string TusManager::TAG_TUS_MAXSZ       = "Tus-Max-Size";
const std::string TusManager::TAG_TUS_EXTENSION   = "Tus-Extension";
const std::string TusManager::TAG_TUS_CHECKSUM_ALG = "Tus-Checksum-Algorithm";
const std::string TusManager::TAG_UPLOAD_LENGTH   = "Upload-Length";
const std::string TusManager::TAG_UPLOAD_METADATA = "Upload-Metadata";
const std::string TusManager::TAG_UPLOAD_OFFSET   = "Upload-Offset";
const std::string TusManager::TAG_UPLOAD_CHECKSUM = "Upload-Checksum";

const std::string TusManager::TUS_SUPPORTED_VERSION       = "1.0.0";
const std::string TusManager::TUS_SUPPORTED_VERSIONS      = "1.0.0";
const std::string TusManager::TUS_SUPPORTED_EXTENSIONS    = "creation,creation-with-upload,terminate,checksum";
const std::string TusManager::TUS_SUPPORTED_CHECKSUM      = "sha1";
const std::string TusManager::TUS_SUPPORTED_CHECKSUMS     = "sha1";
const std::string TusManager::TUS_SUPPORTED_MAXSZ         = "1073741824";
const std::string TusManager::PATCH_EXPECTED_CONTENT_TYPE = "application/offset+octet-stream";

const unsigned Http_Status_Checksum_Mismatch = 460;


http::response<http::dynamic_body>
TusManager::MakeResponse(const http::request<http::dynamic_body>& req)
{
    http::response<http::dynamic_body> resp;
    resp.version(req.version());
    resp.keep_alive(false);
    resp.set(http::field::server, "BeTus 0.1");

    switch (req.method())
    {
    case http::verb::options:
        processOptions(req, resp);
        break;

    case http::verb::head:
        processHead(req, resp);
        break;

    case http::verb::post:
        processPost(req, resp);
        break;

    case http::verb::patch:
        processPatch(req, resp);
        break;

    case http::verb::delete_:
        processDelete(req, resp);
        break;

    default:
        resp.result(http::status::bad_request);
        break;
    }

    resp.set(http::field::content_length, resp.body().size());
    return resp;
}

namespace
{
bool Common_Checks(const http::request<http::dynamic_body>& req,
                   http::response<http::dynamic_body>& resp);
std::pair<bool, size_t>
Patch_Checks(const http::request<http::dynamic_body>& req,
             http::response<http::dynamic_body>& resp);
}

void TusManager::processOptions(const http::request<http::dynamic_body>& req,
                                http::response<http::dynamic_body>& resp)
{
    resp.set(TAG_TUS_RESUMABLE, TusManager::TUS_SUPPORTED_VERSION);

    if (!req.target().starts_with("/files")) // HTTP target is wrong
    {
        resp.result(http::status::not_found);
        return;
    }
    const auto [ho_found, hoststr] = Parse_From_Req(req, http::field::host);
    if (ho_found)
    {
        // TODO: How/What should we check this value against?
        // return;
    }

    resp.set(TAG_TUS_VERSION, TUS_SUPPORTED_VERSIONS);
    resp.set(TAG_TUS_MAXSZ, TUS_SUPPORTED_MAXSZ);
    resp.set(TAG_TUS_EXTENSION, TUS_SUPPORTED_EXTENSIONS);
    resp.set(TAG_TUS_CHECKSUM_ALG, TUS_SUPPORTED_CHECKSUMS);
    resp.result(http::status::no_content);
}

void TusManager::processHead(const http::request<http::dynamic_body>& req,
                             http::response<http::dynamic_body>& resp)
{
    if (!Common_Checks(req, resp)) return;

    const std::string fileUUID(req.target().begin() + strlen("/files/"), req.target().end());
    if (!files_man_.HasFile(fileUUID))
    {
        resp.result(http::status::gone);
        return;
    }
    const auto md = files_man_.GetMetadata(fileUUID);
    if (md.offset < 0)
    {
        resp.result(http::status::gone);
        return;
    }

    resp.set(TAG_UPLOAD_OFFSET, std::to_string(md.offset));
    if (md.length > 0)
        resp.set(TAG_UPLOAD_LENGTH, std::to_string(md.length));
    if (!md.comment.empty())
        resp.set(TAG_UPLOAD_METADATA, md.comment);

    resp.set(http::field::cache_control, "no-store");
    resp.result(http::status::no_content);
}

void TusManager::processPost(const http::request<http::dynamic_body>& req,
                             http::response<http::dynamic_body>& resp)
{
    if (!Common_Checks(req, resp)) return;

    const auto [ul_found, uploadlen] = Parse_Number_From_Req<size_t>(req, TAG_UPLOAD_LENGTH);
    if (!ul_found || uploadlen == 0) // we don't support "Deferred Length" yet
    {
        resp.result(http::status::bad_request);
        return;
    }

    auto newres = files_man_.NewTmpFilesResource();
    {
        const auto [md_found, mtdata] = Parse_From_Req(req, TAG_UPLOAD_METADATA);
        auto& o = newres.MetadataFstream(uploadlen, mtdata);
        if (!o.good())
        {
            std::cerr << "write error: " << newres.MetadataPath() << " couldn't be written/opened" << std::endl;
            resp.result(http::status::internal_server_error);
            return;
        }
    }
    {
        auto& o = newres.DataFstream(uploadlen);
        if (!o.good())
        {
            std::cerr << "write error: " << newres.DataPath() << " couldn't be written/opened" << std::endl;
            resp.result(http::status::internal_server_error);
            return;
        }
    }

    if (const auto [cl_found, contentlen] = Parse_Number_From_Req<size_t>(req, http::field::content_length);
            cl_found && contentlen > 0) // creation-with-upload support
    {
        if (const auto [ct_found, ct_val] = Parse_From_Req(req, http::field::content_type);
                !ct_found || ct_val != TusManager::PATCH_EXPECTED_CONTENT_TYPE) // Content-Type not found or wrong
        {
            resp.result(http::status::unsupported_media_type);
            return;
        }
        auto res = files_man_.Write(newres.Uuid(), 0, req.body());
        if (res < 1)
        {
            std::cerr << "initial write error: " << newres.DataPath() << " couldn't be written/opened" << std::endl;
            resp.result(http::status::internal_server_error);
            return;
        }

        resp.set(TAG_UPLOAD_OFFSET, res);
    }
    files_man_.Persist(newres);

    resp.set(http::field::location, "http://127.0.0.1:8080/files/" + newres.Uuid());
    resp.result(http::status::created);
}

void TusManager::processPatch(const http::request<http::dynamic_body>& req,
                              http::response<http::dynamic_body>& resp)
{
    if (!Common_Checks(req, resp)) return;

    const std::string fileUUID(req.target().begin() + strlen("/files/"), req.target().end());
    const auto [ok, offset_val] = Patch_Checks(req, resp);
    if (!ok) return;

    const auto [uc_found, uc_val] = Parse_From_Req(req, TAG_UPLOAD_CHECKSUM);
    auto uc_spaceit = std::find(uc_val.begin(), uc_val.end(), ' ');
    if (uc_found &&
        !std::equal(std::begin(TUS_SUPPORTED_CHECKSUM), std::end(TUS_SUPPORTED_CHECKSUM),
                    uc_val.begin(), uc_spaceit)) // supported checksum?
    {
        resp.result(http::status::bad_request);
        return;
    }

    if (!files_man_.HasFile(fileUUID))
    {
        resp.result(http::status::not_found);
        return;
    }
    const auto md = files_man_.GetMetadata(fileUUID);
    if (md.offset < 0)
    {
        resp.result(http::status::not_found);
        return;
    }
    if (static_cast<size_t>(md.offset) != offset_val)
    {
        resp.result(http::status::conflict);
        return;
    }
    auto res = files_man_.Write(fileUUID, offset_val, req.body());
    if (res < 1)
    {
        resp.result(http::status::internal_server_error);
        return;
    }

    if (uc_found)
    {
        ++uc_spaceit;
        const auto csbin = Base64_To_Bin(
                               std::string_view(&(*uc_spaceit), uc_val.end() - uc_spaceit));
        const auto cshexstr = files_man_.ChecksumSha1Hex(fileUUID, offset_val, res);
        if (!CheckSum_Match(cshexstr, csbin))
        {
            resp.result(Http_Status_Checksum_Mismatch);
            return;
        }
    }
    if (!files_man_.UpdateOffsetMetadata(fileUUID, offset_val + res))
    {
        resp.result(http::status::internal_server_error);
        return;
    }

    resp.set(TAG_UPLOAD_OFFSET, offset_val + res);
    resp.result(http::status::no_content);
}

void TusManager::processDelete(const http::request<http::dynamic_body>& req,
                               http::response<http::dynamic_body>& resp)
{
    if (!Common_Checks(req, resp)) return;

    if (const auto [clen_found, clen_val] = Parse_Number_From_Req<size_t>(req, http::field::content_length);
            clen_found || clen_val > 0) // Has a content Content-Length
    {
        resp.result(http::status::bad_request);
        std::cerr << "delete Shall have zero Content-Length" << std::endl;
        return;
    }

    const std::string fileUUID(req.target().begin() + strlen("/files/"), req.target().end());
    if (auto res = files_man_.Delete(fileUUID); !res)
    {
        resp.result(http::status::not_found);
        return;
    }
    resp.result(http::status::no_content);
}

namespace
{
bool Common_Checks(const http::request<http::dynamic_body>& req,
                   http::response<http::dynamic_body>& resp)
{
    resp.set(TusManager::TAG_TUS_RESUMABLE, TusManager::TUS_SUPPORTED_VERSION);

    if (!req.target().starts_with("/files")) // HTTP target is wrong
    {
        resp.result(http::status::not_found);
        return false;
    }
    const auto [ho_found, hoststr] = Parse_From_Req(req, http::field::host);
    if (ho_found)
    {
        // TODO: How/What should we check this value against?
        // return;
    }
    const auto [tr_found, tr_val] = Parse_From_Req(req, TusManager::TAG_TUS_RESUMABLE);
    if (!tr_found ||
            !std::equal(tr_val.begin(), tr_val.end(),
                        std::begin(TusManager::TUS_SUPPORTED_VERSION),
                        std::end(TusManager::TUS_SUPPORTED_VERSION))) // Unsupported version received
    {
        resp.result(http::status::precondition_failed);
        return false;
    }

    return true;
}

std::pair<bool, size_t>
Patch_Checks(const http::request<http::dynamic_body>& req,
             http::response<http::dynamic_body>& resp)
{
    std::pair<bool, size_t> ret{false, 0};

    const auto [ct_found, ct_val] = Parse_From_Req(req, http::field::content_type);
    if (!ct_found || ct_val != TusManager::PATCH_EXPECTED_CONTENT_TYPE) // Content-Type not found or wrong
    {
        resp.result(http::status::unsupported_media_type);
        return ret;
    }
    const auto [clen_found, clen_val] = Parse_Number_From_Req<size_t>(req, http::field::content_length);
    if (!clen_found || clen_val == 0) // Content-Length not found or wrong
    {
        resp.result(http::status::bad_request);
        return ret;
    }
    const auto pp = Parse_Number_From_Req<size_t>(req, TusManager::TAG_UPLOAD_OFFSET);
    if (!pp.first) // Upload-Offset not found
    {
        resp.result(http::status::bad_request);
        return ret;
    }

    ret.first = true;
    ret.second = pp.second;
    return ret;
}
}

} // namespace tus
