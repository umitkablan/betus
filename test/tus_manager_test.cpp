#include "include/tus_manager.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;

#include <catch2/catch.hpp>

using tus::TusManager;

TEST_CASE( "Basic", "[TusManager]" )
{
    TusManager tm(".");

    SECTION( "Invalid root request" )
    {
        http::request<http::dynamic_body> req{http::verb::get, "/", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.count(http::field::content_length) == 1);
        CHECK(resp.at(http::field::content_length) == "0");
    }

    SECTION( "Options request - wrong root" )
    {
        http::request<http::dynamic_body> req{http::verb::options, "/", 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        const auto resp = tm.MakeResponse(req);
        REQUIRE(resp.count(http::field::content_length) == 1);
        CHECK(resp.at(http::field::content_length) == "0");
        REQUIRE(resp.count(TusManager::TAG_TUS_RESUMABLE) == 1);
        CHECK(resp.at(TusManager::TAG_TUS_RESUMABLE) == "1.0.0");
    }
}

