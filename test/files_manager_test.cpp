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
            CHECK(fm.UpdateOffsetMetadata(f_uuid, 100));
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

TEST_CASE( "Delete", "[FilesManager]" )
{
    tus::FilesManager fm(".");

    SECTION("returns false when not exists")
    {
        auto res = fm.Delete("nott-exis-tent-file");
        REQUIRE(res == false);
    }

    SECTION("when metadata is empty")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            f_uuid = res.Uuid();
            auto& od = res.DataFstream(1007);
            CHECK(!!od);

            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
        REQUIRE(fm.Delete(f_uuid) == true);
        REQUIRE(fm.Size() == 0);
    }

    SECTION("when data file is empty")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            f_uuid = res.Uuid();
            auto& om = res.MetadataFstream();
            CHECK(!!om);

            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
        REQUIRE(fm.Delete(f_uuid) == true);
        REQUIRE(fm.Size() == 0);
    }

    SECTION("when all full")
    {
        std::string f_uuid;
        {
            auto res = fm.NewTmpFilesResource();
            f_uuid = res.Uuid();
            auto& od = res.DataFstream(1007);
            CHECK(!!od);
            auto& om = res.MetadataFstream();
            CHECK(!!om);

            fm.Persist(res);
        }
        REQUIRE(fm.Size() == 1);
        REQUIRE(fm.Delete(f_uuid) == true);
        REQUIRE(fm.Size() == 0);
    }
}

TEST_CASE("Digest", "[FilesManager]")
{
    tus::FilesManager fm(".");

    SECTION("returns empty when not exists")
    {
        auto res = fm.ChecksumSha1("nott-exis-tent-file");
        REQUIRE(res.empty());
    }

    SECTION("returns empty when file could not be opened")
    {
        // TODO
        // auto res = fm.ChecksumSha1("erro-file-nott-easy");
        // CHECK(res.empty());
    }

    SECTION("returns sha1 of hello world - default parameters")
    {
        const char* fname = "hello-world-to-be-sha1_DELETE";
        {
            std::ofstream of(fname);
            of << "hello world!" << std::endl;
        }
        auto res = fm.ChecksumSha1(fname);
        // Capital letters
        ::remove(fname);
        REQUIRE(res.compare("F951B101989B2C3B7471710B4E78FC4DBDFA0CA6") == 0); // echo "hello world!" | sha1sum
    }

    SECTION("returns empty when begin position is invalid")
    {
        const char* fname = "hello-world-to-be-sha1_DELETE";
        {
            std::ofstream of(fname);
            of << "hello world!" << std::endl;
        }
        auto res = fm.ChecksumSha1(fname, 13);
        ::remove(fname);
        REQUIRE(res.empty());
    }

    SECTION("returns empty when begin-end range is invalid")
    {
        const char* fname = "hello-world-to-be-sha1_DELETE";
        {
            std::ofstream of(fname);
            of << "hello world!" << std::endl;
        }
        auto res = fm.ChecksumSha1(fname, 10, 4);
        ::remove(fname);
        REQUIRE(res.empty());
    }

    SECTION("returns success when begin-end range is valid")
    {
        const char* fname = "hello-world-to-be-sha1_DELETE";
        {
            std::ofstream of(fname);
            of << "hello world!" << std::endl;
        }
        auto res = fm.ChecksumSha1(fname, 10, 3);
        ::remove(fname);
        REQUIRE(res.compare("4C9E2DC5D81E106BB2E5A43B720C1486417C2974") == 0); // echo "d!" | sha1sum
    }
}
