#include "include/files_manager.hpp"
#include <fstream>


#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

TEST_CASE( "In directory where write is not allowed", "[FilesManager]" )
{
    tus::FilesManager fm("/");

    SECTION( "Paths are empty when streams are not acquired" )
    {
        auto res = fm.NewTmpFilesResource();
        REQUIRE(!res.Uuid().empty());
        REQUIRE(res.MetadataPath().empty());
        REQUIRE(res.DataPath().empty());
        REQUIRE(fm.Size() == 1);
    }
    REQUIRE(fm.Size() == 0);

    SECTION( "Acquired streams are not usable" )
    {
        auto res = fm.NewTmpFilesResource();
        auto& om = res.MetadataFstream();
        REQUIRE(!om);
        auto& od = res.DataFstream(1000);
        REQUIRE(!od);
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
            REQUIRE(res.MetadataPath().empty());
            REQUIRE(res.DataPath().empty());
            REQUIRE(fm.Size() == 1);
        }
        REQUIRE(fm.Size() == 0);
    }

    std::string dt_fname, md_fname;
    SECTION( "Streams are usable, not persisted - removed" )
    {
        {
            auto res = fm.NewTmpFilesResource();
            auto& om = res.MetadataFstream();
            REQUIRE(om);
            auto& od = res.DataFstream(1000);
            REQUIRE(od);
            dt_fname = res.MetadataPath();
            md_fname = res.DataPath();
            REQUIRE(!dt_fname.empty());
            REQUIRE(!md_fname.empty());
            REQUIRE(fm.Size() == 1);
        }
        std::fstream dt_fstr(dt_fname);
        CHECK(!dt_fstr);
        std::fstream md_fstr(md_fname);
        CHECK(!md_fstr);

        REQUIRE(fm.Size() == 0);
    }

    SECTION( "Streams are usable, persisted - files are there" )
    {
        {
            auto res = fm.NewTmpFilesResource();
            auto& om = res.MetadataFstream();
            CHECK(om);
            om << "testtesttest" << std::endl;
            auto& od = res.DataFstream(1000);
            CHECK(od);
            dt_fname = res.MetadataPath();
            md_fname = res.DataPath();

            fm.Persist(res);
        }
        std::fstream dt_fstr(dt_fname);
        CHECK(!!dt_fstr);
        std::fstream md_fstr(md_fname);
        CHECK(!!md_fstr);
        ::remove(dt_fname.c_str());
        ::remove(md_fname.c_str());

        REQUIRE(fm.Size() == 1);
    }
}

TEST_CASE( "Write offset", "[FilesManager]" )
{
    namespace beast = boost::beast;

    tus::FilesManager fm(".");

    SECTION( "-1 when file not exists" )
    {
        auto md = fm.GetMetadata("nott-exis-tent-file");
        REQUIRE(md.offset == -1);
        REQUIRE(md.length == 0);
        REQUIRE(md.comment.empty());
    }

    SECTION( "empty metadata" )
    {
        std::string dt_fname, md_fname, f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            auto& om = res.MetadataFstream();
            CHECK(om);
            auto& od = res.DataFstream(1007);
            CHECK(od);
            dt_fname = res.MetadataPath();
            md_fname = res.DataPath();
            f_uuid = res.Uuid();

            fm.Persist(res);
        }
        auto md = fm.GetMetadata(f_uuid);
        REQUIRE(md.offset == 0);
        REQUIRE(md.length == 0);
        REQUIRE(md.comment.empty());

        ::remove(dt_fname.c_str());
        ::remove(md_fname.c_str());
        REQUIRE(fm.Size() == 1);
    }

    SECTION( "Write and get offset" )
    {
        std::string dt_fname, md_fname, f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            auto& om = res.MetadataFstream(100, "write and get offset");
            CHECK(om);
            auto& od = res.DataFstream(1007);
            CHECK(od);
            dt_fname = res.MetadataPath();
            md_fname = res.DataPath();
            f_uuid = res.Uuid();

            fm.Persist(res);
        }
        auto md = fm.GetMetadata(f_uuid);
        REQUIRE(md.offset == 0);
        REQUIRE(md.length == 100);
        REQUIRE(md.comment.compare("write and get offset") == 0);

        {
            beast::multi_buffer mb(100);
            auto mutable_bufs = mb.prepare(100);
            auto sz =  (*mutable_bufs.begin()).size();
            char* dat = static_cast<char*>((*mutable_bufs.begin()).data());
            for (size_t i = 0; i < sz; ++i) dat[i] = 'g';
            mb.commit(100);

            CHECK(fm.Write(f_uuid, 0, mb) == 100);
        }
        md = fm.GetMetadata(f_uuid);
        REQUIRE(md.offset == 100);
        REQUIRE(md.length == 100);
        REQUIRE(md.comment.compare("write and get offset") == 0);

        ::remove(dt_fname.c_str());
        ::remove(md_fname.c_str());
        REQUIRE(fm.Size() == 1);
    }
}

