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

