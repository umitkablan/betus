#include "include/files_manager.hpp"

#include <fstream>
#include <system_error>
#include <thread>


#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

TEST_CASE( "In directory where write is not allowed", "[FilesManager]" )
{
    tus::FilesManager fm("/");

    SECTION("not initialized")
    {
        auto res = fm.NewTmpFilesResource();
        REQUIRE(!res.Uuid().empty());
        REQUIRE(fm.Size() == 1);
    }
    REQUIRE(fm.Size() == 0);

    SECTION("initialized")
    {
        auto res = fm.NewTmpFilesResource();
        auto err = res.Initialize(1000);
        REQUIRE(err == std::errc::bad_file_descriptor);
        REQUIRE(fm.Size() == 1);
    }
    REQUIRE(fm.Size() == 0);
}

TEST_CASE( "Basic with regular writable directory", "[FilesManager]" )
{
    tus::FilesManager fm(".");

    SECTION( "Paths are empty, name is removed" )
    {
        {
            auto res = fm.NewTmpFilesResource();
            REQUIRE(!res.Uuid().empty());
            REQUIRE(fm.Size() == 1);
        }
        REQUIRE(fm.Size() == 0);
    }

    SECTION("initialized not persisted - removed")
    {
        {
            auto res = fm.NewTmpFilesResource();
            auto err = res.Initialize(1000);
            REQUIRE(err == static_cast<std::errc>(0));
            REQUIRE(fm.Size() == 1);
        }
        REQUIRE(fm.Size() == 0);
    }

    SECTION("initialized not persisted - moved to FileResource")
    {
        std::string uuid;
        {
            auto res = fm.NewTmpFilesResource();
            auto err = res.Initialize(1009);
            REQUIRE(err == static_cast<std::errc>(0));
            uuid = res.Uuid();
            REQUIRE(fm.Size() == 1);
            tus::FileResource fres(std::move(res));
            CHECK(fres.Write("hello world!\n"));
            fres.Commit();
        }
        REQUIRE(fm.Size() == 1);
        {
            auto [res, fres] = fm.GetFileResource(uuid);
            CHECK(fres.IsOpen());
            auto sha1sum = fres.ChecksumSha1Hex();
            CHECK(sha1sum.compare("F951B101989B2C3B7471710B4E78FC4DBDFA0CA6") == 0); // echo "hello world!" | sha1sum
            fres.Delete();
            fres.Commit();
        }
        REQUIRE(fm.Size() == 0);
    }

    SECTION("initialized persisted - files are there")
    {
        {
            auto res = fm.NewTmpFilesResource();
            auto err = res.Initialize(1000);
            CHECK(err == static_cast<std::errc>(0));
            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
    }

    fm.RmAllFiles();
}

TEST_CASE( "Write offset", "[FilesManager]" )
{
    namespace beast = boost::beast;

    tus::FilesManager fm(".");

    SECTION("-1 when file not exists")
    {
        auto [res, fres] = fm.GetFileResource("nott-exis-tent-file");
        CHECK(res == std::errc::no_such_file_or_directory);
        CHECK(fres.IsOpen() == false);

        auto md = fres.GetMetadata();
        REQUIRE(md.offset == -1);
        REQUIRE(md.length == 0);
        REQUIRE(md.comment.empty());
    }

    SECTION("empty metadata")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            auto err = res.Initialize(1007);
            CHECK(err == static_cast<std::errc>(0));
            f_uuid = res.Uuid();
            fm.Persist(res);
        }
        auto [res, fres] = fm.GetFileResource(f_uuid);
        CHECK(res == static_cast<std::errc>(0));
        const auto md = fres.GetMetadata();
        CHECK(md.offset == 0);
        CHECK(md.length == 1007);
        CHECK(md.comment.empty());

        CHECK(fm.Size() == 1);
        fm.RmAllFiles();
    }

    SECTION( "Write and get offset" )
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            auto err = res.Initialize(100, "write and get offset");
            CHECK(err == static_cast<std::errc>(0));
            f_uuid = res.Uuid();

            fm.Persist(res);
        }
        {
            auto [res, fres] = fm.GetFileResource(f_uuid);
            const auto md = fres.GetMetadata();
            CHECK(md.offset == 0);
            CHECK(md.length == 100);
            CHECK(md.comment.compare("write and get offset") == 0);
        }
        {
            beast::multi_buffer mb(100);
            auto mutable_bufs = mb.prepare(100);
            char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
            std::fill_n(dat, (*mutable_bufs.begin()).size(), 'g');
            mb.commit(100);

            auto [res, fres] = fm.GetFileResource(f_uuid);
            CHECK(res == static_cast<std::errc>(0));
            CHECK(fres.Write(0, mb) == 100);
            fres.Commit();
        }
        auto [res, fres] = fm.GetFileResource(f_uuid);
        const auto md = fres.GetMetadata();
        CHECK(md.offset == 100);
        CHECK(md.length == 100);
        CHECK(md.comment.compare("write and get offset") == 0);

        CHECK(fm.Size() == 1);
        fm.RmAllFiles();
    }

    fm.RmAllFiles();
}

TEST_CASE( "Delete", "[FilesManager]" )
{
    tus::FilesManager fm(".");

    SECTION("when not exists")
    {
        {
            auto [res, fres] = fm.GetFileResource("nott-exis-tent-file");
            CHECK(res == std::errc::no_such_file_or_directory);
            CHECK(fres.IsOpen() == false);
            fres.Delete();
            fres.Commit();
        }
        {
            auto [res, fres] = fm.GetFileResource("nott-exis-tent-file");
            CHECK(res == std::errc::no_such_file_or_directory);
        }
    }

    SECTION("when metadata is empty")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            f_uuid = res.Uuid();
            auto err = res.Initialize(1007);
            CHECK(err == static_cast<std::errc>(0));

            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
        {
            auto [res, fres] = fm.GetFileResource(f_uuid);
            CHECK(res == static_cast<std::errc>(0));
            CHECK(fres.IsOpen() == true);
            fres.Delete();
            fres.Commit();
        }
        REQUIRE(fm.Size() == 0);
    }

    SECTION("when data file is empty")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            f_uuid = res.Uuid();
            auto err = res.Initialize(109);
            CHECK(err == static_cast<std::errc>(0));

            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
        {
            auto [res, fres] = fm.GetFileResource(f_uuid);
            REQUIRE(fres.IsOpen() == true);
            fres.Delete();
            fres.Commit();
        }
        REQUIRE(fm.Size() == 0);
    }

    SECTION("when all full")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            f_uuid = res.Uuid();
            auto err = res.Initialize(1007);
            CHECK(err == static_cast<std::errc>(0));

            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
        {
            auto [res, fres] = fm.GetFileResource(f_uuid);
            REQUIRE(fres.IsOpen() == true);
            fres.Delete();
            fres.Commit();
        }
        REQUIRE(fm.Size() == 0);
    }
}

TEST_CASE("Digest Error Case", "[FilesManager]")
{
    tus::FilesManager fm(".");

    SECTION("returns empty when not exists")
    {
        auto [res, fres] = fm.GetFileResource("nott-exis-tent-file");
        REQUIRE(res == std::errc::no_such_file_or_directory);
        REQUIRE(fres.IsOpen() == false);
        const auto hashstr = fres.ChecksumSha1Hex();
        REQUIRE(hashstr.empty());
    }

    SECTION("returns empty when file could not be opened")
    {
        // TODO
        // auto res = fm.ChecksumSha1Hex("erro-file-nott-easy");
        // CHECK(res.empty());
    }
}

TEST_CASE("Digest Hello World", "[FilesManager]")
{
    tus::FilesManager fm(".");

    const auto f_uuid = [&fm]() {
        auto res = fm.NewTmpFilesResource();
        const auto uuid = res.Uuid();
        auto err = res.Initialize(107);
        CHECK(err == static_cast<std::errc>(0));

        fm.Persist(res);
        return uuid;
    }();

    auto [res, fres] = fm.GetFileResource(f_uuid);
    CHECK(fres.IsOpen() == true);

    const auto hello_world = std::string_view("hello world!\n", 13);
    CHECK(fres.Write(hello_world) == true);

    SECTION("returns sha1 of hello world - default parameters")
    {
        auto res = fres.ChecksumSha1Hex();
        CHECK(res.compare("F951B101989B2C3B7471710B4E78FC4DBDFA0CA6") == 0); // echo "hello world!" | sha1sum
    }

    SECTION("returns empty when begin position is invalid")
    {
        auto res = fres.ChecksumSha1Hex(13);
        CHECK(res.empty());
    }

    SECTION("returns empty when begin-end range is invalid")
    {
        auto res = fres.ChecksumSha1Hex(10, 4);
        CHECK(res.empty());
    }

    SECTION("returns success when begin-end range is valid")
    {
        auto res = fres.ChecksumSha1Hex(10, 3);
        CHECK(res.compare("4C9E2DC5D81E106BB2E5A43B720C1486417C2974") == 0); // echo "d!" | sha1sum
    }

    REQUIRE(fm.Size() == 1);
    fm.RmAllFiles();
    REQUIRE(fm.Size() == 0);
}
