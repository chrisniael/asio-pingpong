/**
 * @file client.cpp
 * @brief client
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <string>

#include "asio.hpp"

struct Buffer {
  enum { kMaxBufferSize = 16 * 1024 };
  char buffer[kMaxBufferSize];
};

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::io_service& io_service)
      : io_service_(io_service), resolver_(io_service), socket_(io_service) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  virtual ~Session() {}

  asio::ip::tcp::socket& get_socket() { return this->socket_; }

  void Close() {
    this->io_service_.post([=]() {
      this->socket_.close();
      this->OnClose();
    });
  }
  void Write(const Buffer& msg) {
    io_service_.post([=]() {
      bool write_in_progress = !write_bufs_.empty();
      write_bufs_.push_back(msg);
      if (!write_in_progress) {
        DoWrite();
      }
    });
  }

  void DoRead() {
    asio::async_read(
        socket_, asio::buffer(this->read_buf_.buffer, Buffer::kMaxBufferSize),
        [=](std::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            this->OnRead(this->read_buf_);
            DoRead();
          } else {
            RecvError();
          }
        });
  }

  void DoWrite() {
    asio::async_write(
        socket_,
        asio::buffer(write_bufs_.front().buffer, Buffer::kMaxBufferSize),
        [=](std::error_code ec, std::size_t /*length*/) {
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

  virtual void RecvError() {}
  virtual void SendError() {}
  virtual void OnClose() {}
  virtual void OnRead(const Buffer& buf) {
    static long long msg_count = 0;
    static std::chrono::milliseconds time_from =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    std::chrono::milliseconds time_now =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    uint64_t time_passed = (time_now - time_from).count();
    uint64_t pack_per_sec =
        static_cast<double>(++msg_count) / time_passed * 1000;
    std::cout << "Recv msg, count=" << msg_count
              << ", pack/sec=" << pack_per_sec << std::endl;

    this->Write(buf);
  }

 private:
  asio::io_service& io_service_;
  asio::ip::tcp::resolver resolver_;
  asio::ip::tcp::socket socket_;
  Buffer read_buf_;
  std::deque<Buffer> write_bufs_;
};

class Client {
 public:
  Client(asio::io_service& io_service)
      : resolver_(io_service), session_(io_service) {}

  void Connect(const std::string& ip, const unsigned short& port) {
    auto endpoint_iterator =
        this->resolver_.resolve({ip, std::to_string(port)});
    this->DoConnect(endpoint_iterator);
  }

  void DoConnect(asio::ip::tcp::resolver::iterator endpoint_iterator) {
    asio::async_connect(
        this->session_.get_socket(), endpoint_iterator,
        [=](std::error_code ec, asio::ip::tcp::resolver::iterator) {
          if (!ec) {
            this->OnConnected();
            this->session_.DoRead();
          } else {
            ConnError();
          }
        });
  }

  virtual void OnConnected() {
    std::cout << "Connected." << std::endl;
    Buffer buf;
    memset(buf.buffer, 1, Buffer::kMaxBufferSize);
    session_.Write(buf);
  }
  virtual void ConnError() {}

 private:
  asio::ip::tcp::resolver resolver_;
  Session session_;
};

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: client <host> <port>\n";
    return 1;
  }
  std::cout << "Client start." << std::endl;

  std::string ip = argv[1];
  unsigned short port = std::stoi(argv[2]);

  try {
    asio::io_service io_service;
    Client client(io_service);
    client.Connect(ip, port);
    io_service.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
