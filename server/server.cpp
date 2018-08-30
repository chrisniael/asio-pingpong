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

class Msg {
 public:
  enum { kHeaderLength = 2 };
  enum { kMaxBodyLength = 16000 };

  Msg() : body_length_(0) {}

  const char* data() const { return data_; }

  char* data() { return data_; }

  std::size_t length() const { return kHeaderLength + body_length_; }

  const char* body() const { return data_ + kHeaderLength; }

  char* body() { return data_ + kHeaderLength; }

  std::size_t body_length() const { return body_length_; }

  void set_body_length(std::size_t new_length) {
    body_length_ = new_length;
    if (body_length_ > kMaxBodyLength) body_length_ = kMaxBodyLength;

    uint16_t* body_length_pt = reinterpret_cast<uint16_t*>(this->data_);
    *body_length_pt = new_length;
  }

  virtual bool DecodeHeader() {
    uint16_t len = 0;
    memcpy(&len, data_, kHeaderLength);
    body_length_ = len;

    if (body_length_ > kMaxBodyLength) {
      body_length_ = 0;
      return false;
    }
    return true;
  }

 private:
  char data_[kHeaderLength + kMaxBodyLength];
  uint16_t body_length_;
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
  void Write(const Msg& msg) {
    auto self = this->shared_from_this();
    io_service_.post([this, self, msg]() {
      bool write_in_progress = !write_msgs_.empty();
      write_msgs_.push_back(msg);
      if (!write_in_progress) {
        DoWrite();
      }
    });
  }

  void DoReadHeader() {
    auto self = this->shared_from_this();
    asio::async_read(socket_,
                     asio::buffer(read_msg_.data(), Msg::kHeaderLength),
                     [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec && this->OnReadHeader(&read_msg_)) {
                         DoReadBody();
                       } else {
                         RecvError();
                       }
                     });
  }

  void DoReadBody() {
    auto self = this->shared_from_this();
    asio::async_read(socket_,
                     asio::buffer(read_msg_.body(), read_msg_.body_length()),
                     [this, self](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                         this->OnReadBody(&read_msg_);
                         DoReadHeader();
                       } else {
                         RecvError();
                       }
                     });
  }
  void DoWrite() {
    auto self = this->shared_from_this();
    asio::async_write(
        socket_,
        asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        [this, self](std::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            write_msgs_.pop_front();
            if (!write_msgs_.empty()) {
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
  virtual bool OnReadHeader(Msg* msg) {
    if (!msg) return false;
    return msg->DecodeHeader();
  }
  virtual void OnReadBody(Msg* msg) {
    if (!msg) return;

    static long long msg_count = 0;
    std::cout << "Recv msg, count=" << msg_count << std::endl;
    this->Write(*msg);
  }

 private:
  asio::io_service& io_service_;
  asio::ip::tcp::socket socket_;
  Msg read_msg_;
  std::deque<Msg> write_msgs_;
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
        session->DoReadHeader();
        DoAccept();
      } else {
        std::cerr << "Accept error." << std::endl;
      }
    });
  }

 private:
  asio::ip::tcp::acceptor acceptor_;
};

int main() {
  std::cout << "Server." << std::endl;
  std::string ip = "0.0.0.0";
  unsigned short port = 6789;

  try {
    asio::io_service io_service;
    Server server(io_service, ip, port);
    server.DoAccept();
    io_service.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
