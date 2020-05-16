#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "include/tus_manager.hpp"

#include <chrono>
#include <iostream>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace tus
{

static TusManager tus_("files");


class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
    tcp::socket socket_;
    asio::steady_timer deadline_;
    beast::flat_buffer buffer_{4096};

    http::request<http::dynamic_body> request_;
    http::response<http::dynamic_body> response_;

public:
    HttpConnection(tcp::socket socket)
        : socket_(std::move(socket)), deadline_{socket_.get_executor(), std::chrono::seconds(60)}
    {
    }

    void handle_request()
    {
        auto self = shared_from_this();

        read_reply_request_async(self);
        set_socket_timeout(self);
    }

private:
    void read_reply_request_async(const std::shared_ptr<HttpConnection>& self)
    {
        http::async_read( socket_, buffer_, request_,
                          [this, self](beast::error_code ec, std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);
            if (!ec)
            {
                response_ = tus_.MakeResponse(request_);
                write_response_async(self);
            }
        });
    }

    void write_response_async(const std::shared_ptr<HttpConnection>& self)
    {
        http::async_write( socket_, response_,
                           [this, self](beast::error_code ec, std::size_t sz)
        {
            if (!ec)
            {
                socket_.shutdown(tcp::socket::shutdown_send, ec);
                deadline_.cancel();
            }
        });
    }

    void set_socket_timeout(const std::shared_ptr<HttpConnection>& self)
    {
        deadline_.async_wait(
            [this, self](beast::error_code ec)
        {
            if (!ec)
            {
                socket_.close(ec);
            }
        });
    }
};

void http_server(tcp::acceptor& acceptor, tcp::socket& socket)
{
    acceptor.async_accept(socket,
                          [&acceptor, &socket](beast::error_code ec)
    {
        if (!ec)
            std::make_shared<HttpConnection>(std::move(socket))->handle_request();
        else
            std::cerr << "Error while async_accept on acceptor: " << ec.message() << '\n';
        http_server(acceptor, socket);
    });
}
} // namespace tus

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <address> <port>\n";
        std::cerr << "  For IPv4, try:\n";
        std::cerr << "    receiver 0.0.0.0 80\n";
        std::cerr << "  For IPv6, try:\n";
        std::cerr << "    receiver 0::0 80\n";

        return EXIT_FAILURE;
    }

    try
    {
        auto const address = asio::ip::make_address(argv[1]);
        unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));

        asio::io_context ioc{1};

        tcp::acceptor acceptor{ioc, {address, port}};
        tcp::socket socket{ioc};
        tus::http_server(acceptor, socket);

        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

