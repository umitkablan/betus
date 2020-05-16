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
    static const std::string TAG_UPLOAD_LENGTH;
    static const std::string TAG_UPLOAD_METADATA;
    static const std::string TAG_UPLOAD_OFFSET;

    static const std::string PATCH_EXPECTED_CONTENT_TYPE;

    TusManager(const std::string& dirpath) : files_man_(dirpath) {}

    boost::beast::http::response<boost::beast::http::dynamic_body> MakeResponse(const boost::beast::http::request<boost::beast::http::dynamic_body>& req);

private:
    void processPost(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                     boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
    void processPatch(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                      boost::beast::http::response<boost::beast::http::dynamic_body>& resp);
};

} // namespace tus
