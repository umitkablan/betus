#include "include/tus_manager.hpp"

#include <boost/asio.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>

#include <iostream>
#include <charconv>
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
}

namespace tus
{

const std::string TusManager::TAG_TUS_RESUMABLE   = "Tus-Resumable";
const std::string TusManager::TAG_TUS_VERSION     = "Tus-Version";
const std::string TusManager::TAG_TUS_MAXSZ       = "Tus-Max-Size";
const std::string TusManager::TAG_TUS_EXTENSION   = "Tus-Extension";
const std::string TusManager::TAG_UPLOAD_LENGTH   = "Upload-Length";
const std::string TusManager::TAG_UPLOAD_METADATA = "Upload-Metadata";
const std::string TusManager::TAG_UPLOAD_OFFSET   = "Upload-Offset";

const std::string TusManager::TUS_SUPPORTED_VERSIONS      = "1.0.0";
const std::string TusManager::TUS_SUPPORTED_EXTENSIONS    = "creation,creation-with-upload";
const std::string TusManager::TUS_SUPPORTED_MAXSZ         = "1073741824";
const std::string TusManager::PATCH_EXPECTED_CONTENT_TYPE = "application/offset+octet-stream";

http::response<http::dynamic_body> TusManager::MakeResponse(const http::request<http::dynamic_body>& req)
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
    if (!req.target().starts_with("/files"))
    {
        // HTTP target is wrong
        resp.result(http::status::not_found);
        return;
    }
    const auto [ho_found, hoststr] = Parse_From_Req(req, http::field::host);
    if (ho_found)
    {
        // TODO: How/What should we check this value against?
        // return;
    }

    resp.set(TAG_TUS_RESUMABLE, "1.0.0");
    resp.set(TAG_TUS_VERSION, TUS_SUPPORTED_VERSIONS);
    resp.set(TAG_TUS_MAXSZ, TUS_SUPPORTED_MAXSZ);
    resp.set(TAG_TUS_EXTENSION, TUS_SUPPORTED_EXTENSIONS);
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

    resp.set(TAG_TUS_RESUMABLE, "1.0.0");
    resp.set(TAG_UPLOAD_OFFSET, std::to_string(md.offset));
    if (md.length > 0)
        resp.set(TAG_UPLOAD_LENGTH, std::to_string(md.length));
    if (!md.comment.empty())
        resp.set(TAG_UPLOAD_METADATA, md.comment);

    resp.set(http::field::cache_control, "no-store");
    resp.result(http::status::created);
}

void TusManager::processPost(const http::request<http::dynamic_body>& req,
                             http::response<http::dynamic_body>& resp)
{
    if (!Common_Checks(req, resp)) return;

    const auto [ul_found, uploadlen] = Parse_Number_From_Req<size_t>(req, TAG_UPLOAD_LENGTH);
    if (!ul_found || uploadlen == 0)
    {
        // we don't support "Deferred Length" yet
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

    const auto [cl_found, contentlen] = Parse_Number_From_Req<size_t>(req, http::field::content_length);
    if (cl_found && contentlen > 0)
    {
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

    resp.set(TAG_TUS_RESUMABLE, "1.0.0");
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

    resp.set(TAG_TUS_RESUMABLE, "1.0.0");
    resp.set(TAG_UPLOAD_OFFSET, offset_val + res);
    resp.result(http::status::no_content);
}

namespace
{
bool Common_Checks(const http::request<http::dynamic_body>& req,
                   http::response<http::dynamic_body>& resp)
{
    if (!req.target().starts_with("/files"))
    {
        // HTTP target is wrong
        resp.result(http::status::not_found);
        return false;
    }
    const auto [tr_found, tr_val] = Parse_From_Req(req, TusManager::TAG_TUS_RESUMABLE);
    if (!tr_found)
    {
        // TusManager-Resumable tag not found
        resp.result(http::status::bad_request);
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
    if (!ct_found || ct_val != TusManager::PATCH_EXPECTED_CONTENT_TYPE)
    {
        // Content-Type not found or wrong
        resp.result(http::status::unsupported_media_type);
        return ret;
    }
    const auto [clen_found, clen_val] = Parse_Number_From_Req<size_t>(req, http::field::content_length);
    if (!clen_found || clen_val == 0)
    {
        // Content-Length not found or wrong
        resp.result(http::status::bad_request);
        return ret;
    }
    const auto pp = Parse_Number_From_Req<size_t>(req, TusManager::TAG_UPLOAD_OFFSET);
    if (!pp.first)
    {
        // Upload-Offset not found
        resp.result(http::status::bad_request);
        return ret;
    }

    ret.first = true;
    ret.second = pp.second;
    return ret;
}
}

} // namespace tus
