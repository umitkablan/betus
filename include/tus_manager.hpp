#pragma once

#include "include/files_manager.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <string>
#include <unordered_set>

namespace tus
{

class TusManager
{
    FilesManager files_man_;

public:
    static const std::string TAG_TUS_RESUMABLE;
    static const std::string TAG_TUS_VERSION;
    static const std::string TAG_TUS_MAXSZ;
    static const std::string TAG_TUS_EXTENSION;
    static const std::string TAG_UPLOAD_LENGTH;
    static const std::string TAG_UPLOAD_METADATA;
    static const std::string TAG_UPLOAD_OFFSET;
    static const std::string TAG_UPLOAD_CHECKSUM;

    static const std::string TUS_SUPPORTED_VERSION;
    static const std::string TUS_SUPPORTED_VERSIONS;
    static const std::string TUS_SUPPORTED_EXTENSIONS;
    static const std::string TUS_SUPPORTED_MAXSZ;
    static const std::string PATCH_EXPECTED_CONTENT_TYPE;

    TusManager(const std::string& dirpath) : files_man_(dirpath) {}

    boost::beast::http::response<boost::beast::http::dynamic_body> MakeResponse(const boost::beast::http::request<boost::beast::http::dynamic_body>& req);

private:
    void processOptions(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                        boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
    void processHead(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                     boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
    void processPost(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                     boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
    void processPatch(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                      boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
    void processDelete(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                       boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
};

} // namespace tus
