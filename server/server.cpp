/**
 * @file server.cpp
 * @brief server
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>

#include "asio.hpp"

struct Buffer {
  enum { kMaxBufferSize = 16 * 1024 };
  char buffer[kMaxBufferSize];
};

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::io_service& io_service)
      : io_service_(io_service), socket_(io_service) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  virtual ~Session() {}

  asio::ip::tcp::socket& get_socket() { return this->socket_; }

  void Close() {
    auto self = this->shared_from_this();
    this->io_service_.post([this, self]() {
      this->socket_.close();
      this->OnClose();
    });
  }
  void Write(const Buffer& msg) {
    auto self = this->shared_from_this();
    io_service_.post([this, self, msg]() {
      bool write_in_progress = !write_bufs_.empty();
      write_bufs_.push_back(msg);
      if (!write_in_progress) {
        DoWrite();
      }
    });
  }

  void DoRead() {
    auto self = this->shared_from_this();
    asio::async_read(socket_,
                     asio::buffer(read_buf_.buffer, Buffer::kMaxBufferSize),
                     [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                         this->OnRead(read_buf_);
                         DoRead();
                       } else {
                         RecvError();
                       }
                     });
  }

  void DoWrite() {
    auto self = this->shared_from_this();
    asio::async_write(
        socket_,
        asio::buffer(write_bufs_.front().buffer, Buffer::kMaxBufferSize),
        [this, self](std::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            write_bufs_.pop_front();
            if (!write_bufs_.empty()) {
              DoWrite();
            }
          } else {
            SendError();
          }
        });
  }

  virtual void RecvError() { std::cerr << "RecvError." << std::endl; }
  virtual void SendError() { std::cerr << "SendError." << std::endl; }
  virtual void OnClose() { std::cout << "OnClose." << std::endl; }
  virtual void OnRead(const Buffer& buf) {
    static long long msg_count = 0;
    std::cout << "Recv msg, count=" << msg_count << std::endl;
    this->Write(buf);
  }

 private:
  asio::io_service& io_service_;
  asio::ip::tcp::socket socket_;
  Buffer read_buf_;
  std::deque<Buffer> write_bufs_;
};

class Server {
 public:
  Server(asio::io_service& io_service, const std::string& ip,
         const unsigned short& port)
      : acceptor_(io_service) {
    asio::ip::address addr;
    addr.from_string(ip);
    asio::ip::tcp::endpoint endpoint(addr, port);
    this->acceptor_.open(asio::ip::tcp::v4());
    asio::socket_base::reuse_address option(true);
    this->acceptor_.set_option(option);
    this->acceptor_.bind(endpoint);
    this->acceptor_.listen();
  }

  void DoAccept() {
    auto session = std::make_shared<Session>(this->acceptor_.get_io_service());
    acceptor_.async_accept(session->get_socket(), [=](std::error_code ec) {
      std::cout << "DoAccept." << std::endl;
      if (!ec) {
        session->DoRead();
        DoAccept();
      } else {
        std::cerr << "Accept error." << std::endl;
      }
    });
  }

 private:
  asio::ip::tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: server <port>\n";
    return 1;
  }
  std::cout << "Server start." << std::endl;
  std::string ip = "0.0.0.0";
  unsigned short port = std::stoi(argv[1]);

  try {
    asio::io_service io_service;
    Server server(io_service, ip, port);
    server.DoAccept();
    io_service.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}