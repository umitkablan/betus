#include "include/tus_manager.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>

#include <iostream>
#include <string>
#include <string_view>

#include <catch2/catch.hpp>
#include <system_error>

namespace beast = boost::beast;
namespace http = beast::http;

using tus::FilesManager;
using tus::TusManager;

namespace
{
void Check_Tus_Header_NoContent(const http::response<http::dynamic_body>& resp)
{
    REQUIRE(resp.count("Tus-Resumable") == 1);
    CHECK(resp.at("Tus-Resumable") == "1.0.0");
    if (resp.count(http::field::content_length) == 1)
        CHECK(resp.at(http::field::content_length) == "0");
}

void Fill_Req(http::request<http::dynamic_body>& req, const std::string_view& content_type = "")
{
    req.set(http::field::host, "localhost");
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (!content_type.empty())
        req.set(http::field::content_type, content_type);
    req.set("Tus-Resumable", "1.0.0");
}
}

TEST_CASE("Basic", "[TusManager]")
{
    FilesManager fm(".");
    TusManager tm(fm);

    SECTION("GET is invalid")
    {
        http::request<http::dynamic_body> req{http::verb::get, "/", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 400);
        REQUIRE(resp.count(http::field::content_length) == 1);
        CHECK(resp.at(http::field::content_length) == "0");
    }

    REQUIRE(tm.DeleteAllFiles() == 0);
}

TEST_CASE("OPTIONS", "[TusManager]")
{
    FilesManager fm(".");
    TusManager tm(fm);

    SECTION("Wrong root")
    {
        http::request<http::dynamic_body> req{http::verb::options, "/", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 404);
        REQUIRE(resp.count(http::field::content_length) == 1);
        CHECK(resp.at(http::field::content_length) == "0");
        REQUIRE(resp.count("Tus-Resumable") == 1);
        CHECK(resp.at("Tus-Resumable") == "1.0.0");
    }

    SECTION("Success")
    {
        http::request<http::dynamic_body> req{http::verb::options, "/files", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 204);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Tus-Version") == 1);
        CHECK(resp.at("Tus-Version") == "1.0.0");
        REQUIRE(resp.count("Tus-Extension") == 1);
        CHECK(resp.at("Tus-Extension") == "creation,creation-with-upload,terminate,checksum");
        REQUIRE(resp.count("Tus-Checksum-Algorithm") == 1);
        CHECK(resp.at("Tus-Checksum-Algorithm") == "sha1");
    }

    REQUIRE(tm.DeleteAllFiles() == 0);
}

TEST_CASE("POST", "[TusManager]")
{
    SECTION("Resource directory is not writable")
    {
        FilesManager fm("/");
        TusManager tm(fm);
        http::request<http::dynamic_body> req{http::verb::post, "/files", 11};
        Fill_Req(req);
        req.set("Upload-Length", 12);

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 500);

        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("location") == 0);

        REQUIRE(tm.DeleteAllFiles() == 0);
    }

    FilesManager fm(".");
    TusManager tm(fm);

    SECTION("Wrong root")
    {
        http::request<http::dynamic_body> req{http::verb::post, "/", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 404);
        Check_Tus_Header_NoContent(resp);
    }

    http::request<http::dynamic_body> req{http::verb::post, "/files", 11};
    req.set(http::field::host, "localhost");
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    SECTION("No Tus-Resumable")
    {
        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 412);
        Check_Tus_Header_NoContent(resp);
    }

    req.set("Tus-Resumable", "1.0.0");

    SECTION("Wrong Upload-Length")
    {
        {
            const auto resp = tm.MakeResponse(req);
            CHECK(resp.result_int() == 400);
            Check_Tus_Header_NoContent(resp);
        }
        req.set("Upload-Length", 0);
        {
            const auto resp = tm.MakeResponse(req);
            CHECK(resp.result_int() == 400);
            Check_Tus_Header_NoContent(resp);
        }
    }

    SECTION("no content_type for initial load within")
    {
        req.set("Upload-Length", 12);

        const char* hwstr = "Hello";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 415);

        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("location") == 0);

        REQUIRE(tm.DeleteAllFiles() == 0);
    }

    SECTION("Success")
    {
        req.set("Upload-Length", 12);

        const auto resp = tm.MakeResponse(req);
        CHECK(resp.result_int() == 201);

        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("location") == 1);

        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    tm.DeleteAllFiles();
}

TEST_CASE("HEAD", "[TusManager]")
{
    FilesManager fm(".");
    TusManager tm(fm);

    http::request<http::dynamic_body> poreq{http::verb::post, "/files", 11};
    Fill_Req(poreq);
    poreq.set("Upload-Length", 12);

    const auto poresp = tm.MakeResponse(poreq);
    CHECK(poresp.result_int() == 201);
    Check_Tus_Header_NoContent(poresp);

    REQUIRE(poresp.count("location") == 1);
    const auto locsw = poresp.at("location");
    auto pos = locsw.find("://");
    REQUIRE(pos != std::string::npos);
    auto it = std::find(locsw.begin() + pos + 3, locsw.end(), '/');
    REQUIRE(it != locsw.end());
    std::string location(it, locsw.end());

    SECTION("Success")
    {
        http::request<http::dynamic_body> req{http::verb::head, location, 11 };
        Fill_Req(req);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 204);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 1);
        CHECK(resp.at("Upload-Offset") == "0");
    }
    SECTION("Resource is busy")
    {
        http::request<http::dynamic_body> req{http::verb::head, location, 11 };
        Fill_Req(req);

        const auto [res, fres] = fm.GetFileResource(location.substr(strlen("/files/")));
        CHECK(!std::make_error_code(res));
        CHECK(fres.IsOpen());

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 409);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 0);
    }
    SECTION("No such resource")
    {
        http::request<http::dynamic_body> req{http::verb::head, location + "_NOT_FOUND", 11 };
        Fill_Req(req);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 404);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 0);
    }
    SECTION("internal error - artificial")
    {
        { // remove file as if it was suddenly gone or corrupted
            auto res = ::remove(location.data() + strlen("/files/"));
            CHECK(res == 0);
        }
        http::request<http::dynamic_body> req{http::verb::head, location, 11 };
        Fill_Req(req);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 500);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 0);
    }
    SECTION("metadata error - artificial")
    {
        { // clear metadata file artificially
            auto of = std::ofstream(location.substr(strlen("/files/")) + ".mdata");
            CHECK(of.is_open());
        }
        http::request<http::dynamic_body> req{http::verb::head, location, 11 };
        Fill_Req(req);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 410);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 0);
    }

    tm.DeleteAllFiles();
}

TEST_CASE("PATCH", "[TusManager]")
{
    FilesManager fm(".");
    TusManager tm(fm);

    http::request<http::dynamic_body> poreq{http::verb::post, "/files", 11};
    Fill_Req(poreq);
    poreq.set("Upload-Length", 11); // hello world

    const auto poresp = tm.MakeResponse(poreq);
    CHECK(poresp.result_int() == 201);
    Check_Tus_Header_NoContent(poresp);

    REQUIRE(poresp.count("location") == 1);
    const auto locsw = poresp.at("location");
    auto pos = locsw.find("://");
    REQUIRE(pos != std::string::npos);
    auto it = std::find(locsw.begin() + pos + 3, locsw.end(), '/');
    REQUIRE(it != locsw.end());
    std::string location(it, locsw.end());

    SECTION("To a non-existing resource")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location + "-123a", 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");

        const char* hwstr = "Hello Sunny World!";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 404);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Sent content is larger than declared")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");

        const char* hwstr = "Hello Sunny World!";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 409);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Wrong offset")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "1");

        const char* hwstr = "Hello Word"; // We are sending offset 1, 1 less char here
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 409);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Wrong content type")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+json");
        req.set("Upload-Offset", "0");

        const char* hwstr = "Hello World";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 415);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("No Upload-Offset")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");

        const char* hwstr = "Hello World";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 400);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("artificial file corruption - deleted")
    {
        { // remove file as if it was suddenly gone or corrupted
            auto res = ::remove(location.data() + strlen("/files/"));
            CHECK(res == 0);
        }

        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");

        const char* hwstr = "Hello World";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 500);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Resource is busy")
    {
        const auto [res, fres] = fm.GetFileResource(location.substr(strlen("/files/")));
        CHECK(!std::make_error_code(res));
        CHECK(fres.IsOpen());

        http::request<http::dynamic_body> req{http::verb::patch, location, 11 };
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");

        const char* hwstr = "Hello World";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 409);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 0);
    }

    SECTION("Correct - two loads")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");

        {
            req.set("Upload-Offset", "0");

            const char *hwstr = "Hello ";
            req.content_length(strlen(hwstr));

            beast::multi_buffer mb(100);
            auto mutable_bufs = mb.prepare(strlen(hwstr));
            char *dat = static_cast<char *>((*mutable_bufs.begin()).data());
            memcpy(dat, hwstr, strlen(hwstr));
            mb.commit(strlen(hwstr));

            req.body() = mb;

            const auto resp = tm.MakeResponse(req);

            CHECK(resp.result_int() == 204);
            Check_Tus_Header_NoContent(resp);
            REQUIRE(resp.count("Upload-Offset") == 1);
            CHECK(resp.at("Upload-Offset") == "6");
        }
        {
            req.set("Upload-Offset", "6");

            const char *hwstr = "World";
            req.content_length(strlen(hwstr));

            beast::multi_buffer mb(100);
            auto mutable_bufs = mb.prepare(strlen(hwstr));
            char *dat = static_cast<char *>((*mutable_bufs.begin()).data());
            memcpy(dat, hwstr, strlen(hwstr));
            mb.commit(strlen(hwstr));

            req.body() = mb;

            const auto resp = tm.MakeResponse(req);

            CHECK(resp.result_int() == 204);
            Check_Tus_Header_NoContent(resp);
        }

        REQUIRE(tm.DeleteAllFiles() == 1);
    }
}

TEST_CASE("Checksum", "[TusManager]")
{
    FilesManager fm(".");
    TusManager tm(fm);

    http::request<http::dynamic_body> poreq{http::verb::post, "/files", 11};
    Fill_Req(poreq);
    poreq.set("Upload-Length", 11); // hello world

    const auto poresp = tm.MakeResponse(poreq);
    CHECK(poresp.result_int() == 201);
    Check_Tus_Header_NoContent(poresp);

    REQUIRE(poresp.count("location") == 1);
    const auto locsw = poresp.at("location");
    auto pos = locsw.find("://");
    REQUIRE(pos != std::string::npos);
    auto it = std::find(locsw.begin() + pos + 3, locsw.end(), '/');
    REQUIRE(it != locsw.end());
    std::string location(it, locsw.end());

    SECTION("Wrong Checksum Algorithm")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");
        req.set("Upload-Checksum", "md5 XrY7u+Ae7tCTyyK7j1rNww=="); //echo -n "hello world" | md5sum | xxd -r -p | base64

        const char* hwstr = "hello world";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 400);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("md5 faked as sha1 checksum")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");
        req.set("Upload-Checksum", "sha1 XrY7u+Ae7tCTyyK7j1rNww=="); //echo -n "hello world" | md5sum | xxd -r -p | base64

        const char* hwstr = "hello world";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 460);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Wrong Hash")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");
        req.set("Upload-Checksum", "sha1 Kq5sNclPz7QV2+lfQIuc6R7oRu0=");

        const char* hwstr = "Hello word!";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 460);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Correct Hash - one load")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");
        req.set("Upload-Offset", "0");
        req.set("Upload-Checksum", "sha1 Kq5sNclPz7QV2+lfQIuc6R7oRu0=");

        const char* hwstr = "hello world";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 204);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    SECTION("Correct Hash - two loads")
    {
        http::request<http::dynamic_body> req{http::verb::patch, location, 11};
        Fill_Req(req, "application/offset+octet-stream");

        {
            req.set("Upload-Offset", "0");
            req.set("Upload-Checksum", "sha1 xNhxrROtAP3pp7t/9+0lQ67FQkE="); // echo -n "hello " | sha1sum | xxd -r -p | base64

            const char *hwstr = "hello ";
            req.content_length(strlen(hwstr));

            beast::multi_buffer mb(100);
            auto mutable_bufs = mb.prepare(strlen(hwstr));
            char *dat = static_cast<char *>((*mutable_bufs.begin()).data());
            memcpy(dat, hwstr, strlen(hwstr));
            mb.commit(strlen(hwstr));

            req.body() = mb;

            const auto resp = tm.MakeResponse(req);

            CHECK(resp.result_int() == 204);
            Check_Tus_Header_NoContent(resp);
            REQUIRE(resp.count("Upload-Offset") == 1);
            CHECK(resp.at("Upload-Offset") == "6");
        }
        {
            req.set("Upload-Offset", "6");
            req.set("Upload-Checksum", "sha1 fCEUM/AgcVl3Qeb/Wo6jR4mrv0M="); // echo -n "world" | sha1sum | xxd -r -p | base64

            const char *hwstr = "world";
            req.content_length(strlen(hwstr));

            beast::multi_buffer mb(100);
            auto mutable_bufs = mb.prepare(strlen(hwstr));
            char *dat = static_cast<char *>((*mutable_bufs.begin()).data());
            memcpy(dat, hwstr, strlen(hwstr));
            mb.commit(strlen(hwstr));

            req.body() = mb;

            const auto resp = tm.MakeResponse(req);

            CHECK(resp.result_int() == 204);
            Check_Tus_Header_NoContent(resp);
        }

        REQUIRE(tm.DeleteAllFiles() == 1);
    }

    tm.DeleteAllFiles();
}

TEST_CASE("Terminate Extension", "[TusManager]")
{
    FilesManager fm(".");
    TusManager tm(fm);

    SECTION("File Not Found")
    {
        http::request<http::dynamic_body> req{http::verb::delete_, "/files/aaaa-bbbb-cccc", 11};
        Fill_Req(req);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 404);
    }

    SECTION("Terminate with content should fail")
    {
        http::request<http::dynamic_body> req{http::verb::delete_, "/files/aaaa-bbbb-cccc", 11};
        Fill_Req(req);

        const char *hwstr = "world";
        req.content_length(strlen(hwstr));

        beast::multi_buffer mb(100);
        auto mutable_bufs = mb.prepare(strlen(hwstr));
        char *dat = static_cast<char *>((*mutable_bufs.begin()).data());
        memcpy(dat, hwstr, strlen(hwstr));
        mb.commit(strlen(hwstr));

        req.body() = mb;

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 400);
    }

    http::request<http::dynamic_body> req{http::verb::post, "/files", 11};
    Fill_Req(req, "application/offset+octet-stream");
    req.set("Upload-Length", 11); // hello world
    req.set("Upload-Offset", "0");

    const char* hwstr = "hello world";
    beast::multi_buffer mb(100);
    auto mutable_bufs = mb.prepare(strlen(hwstr));
    char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
    memcpy(dat, hwstr, strlen(hwstr));
    mb.commit(strlen(hwstr));

    req.body() = mb;
    req.content_length(strlen(hwstr));

    const auto resp = tm.MakeResponse(req);
    CHECK(resp.result_int() == 201);
    Check_Tus_Header_NoContent(resp);

    REQUIRE(resp.count("location") == 1);
    const auto locsw = resp.at("location");
    auto pos = locsw.find("://");
    REQUIRE(pos != std::string::npos);
    auto it = std::find(locsw.begin() + pos + 3, locsw.end(), '/');
    REQUIRE(it != locsw.end());
    std::string location(it, locsw.end());

    SECTION("Resource is busy")
    {
        const auto [res, fres] = fm.GetFileResource(location.substr(strlen("/files/")));
        CHECK(!std::make_error_code(res));
        REQUIRE(fres.IsOpen());

        http::request<http::dynamic_body> req{http::verb::delete_, location, 11};
        Fill_Req(req);
        req.content_length(0);

        const auto resp = tm.MakeResponse(req);

        CHECK(resp.result_int() == 409);
        Check_Tus_Header_NoContent(resp);
    }

    SECTION("Success")
    {
        { // Send delete to rm file: Found and Terminated
            http::request<http::dynamic_body> req{http::verb::delete_, location, 11};
            Fill_Req(req);
            req.content_length(0);

            const auto resp = tm.MakeResponse(req);

            CHECK(resp.result_int() == 204);
            Check_Tus_Header_NoContent(resp);
        }
        { // Send another delete to rm file: Not Found
            http::request<http::dynamic_body> req{http::verb::delete_, location, 11};
            Fill_Req(req);
            req.content_length(0);

            const auto resp = tm.MakeResponse(req);

            CHECK(resp.result_int() == 404);
            Check_Tus_Header_NoContent(resp);
        }

        REQUIRE(tm.DeleteAllFiles() == 0);
    }

    tm.DeleteAllFiles();
}
