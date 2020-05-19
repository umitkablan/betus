#include "include/tus_manager.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <iostream>
#include <string>
#include <string_view>

#include <catch2/catch.hpp>

namespace beast = boost::beast;
namespace http = beast::http;

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
}

TEST_CASE("Basic", "[TusManager]")
{
    TusManager tm(".");

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
    TusManager tm(".");

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
    }

    REQUIRE(tm.DeleteAllFiles() == 0);
}

TEST_CASE("POST", "[TusManager]")
{
    TusManager tm(".");

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
    TusManager tm(".");

    http::request<http::dynamic_body> poreq{http::verb::post, "/files", 11};
    poreq.set(http::field::host, "localhost");
    poreq.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    poreq.set("Tus-Resumable", "1.0.0");
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
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set("Tus-Resumable", "1.0.0");

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.result_int() == 204);
        Check_Tus_Header_NoContent(resp);
        REQUIRE(resp.count("Upload-Offset") == 1);
        CHECK(resp.at("Upload-Offset") == "0");
    }

    tm.DeleteAllFiles();
}
