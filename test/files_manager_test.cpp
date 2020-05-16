#include "include/files_manager.hpp"
#include <fstream>


#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

TEST_CASE( "FilesManager_On_Dir_SysRoot_NonAccess", "[single-file]" )
{
    tus::FilesManager fm("/");
    {
        auto res = fm.NewTmpFilesResource();
        REQUIRE(!res.Uuid().empty());
        REQUIRE(res.MetadataPath().empty());
        REQUIRE(res.DataPath().empty());
        REQUIRE(fm.Size() == 1);
    }
    REQUIRE(fm.Size() == 0);
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

TEST_CASE( "FilesManager_On_Regular_Dir", "[single-file]" )
{
    tus::FilesManager fm(".");
    {
        auto res = fm.NewTmpFilesResource();
        REQUIRE(!res.Uuid().empty());
        REQUIRE(res.MetadataPath().empty());
        REQUIRE(res.DataPath().empty());
        REQUIRE(fm.Size() == 1);
    }
    REQUIRE(fm.Size() == 0);

    std::string dt_fname, md_fname;
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
    REQUIRE(fm.Size() == 0);
    {
        std::fstream dt_fstr(dt_fname);
        REQUIRE(!dt_fstr);
        std::fstream md_fstr(md_fname);
        REQUIRE(!dt_fstr);
    }

    {
        auto res = fm.NewTmpFilesResource();
        auto& om = res.MetadataFstream();
        REQUIRE(om);
        om << "testtesttest" << std::endl;
        auto& od = res.DataFstream(1000);
        REQUIRE(od);
        dt_fname = res.MetadataPath();
        md_fname = res.DataPath();
        fm.Persist(res);
    }
    {
        std::fstream dt_fstr(dt_fname);
        REQUIRE(!!dt_fstr);
        std::fstream md_fstr(md_fname);
        REQUIRE(!!dt_fstr);
    }
    REQUIRE(fm.Size() == 1);
}

