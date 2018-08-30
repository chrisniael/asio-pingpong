/**
 * @file server.cpp
 * @brief server
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>

#include "asio.hpp"

class Msg {
 public:
  enum { kHeaderLength = 2 };
  enum { kMaxBodyLength = 5120 };

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
  Session(asio::ip::tcp::socket&& socket)
      : io_service_(socket.get_io_service()),
        resolver_(io_service_),
        socket_(std::move(socket)) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  virtual ~Session() {}

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
                         RecvError(ec);
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
                         RecvError(ec);
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
            SendError(ec);
          }
        });
  }

  virtual void ConnError(const std::error_code& ec) {}
  virtual void RecvError(const std::error_code& ec) {}
  virtual void SendError(const std::error_code& ec) {}
  virtual void OnConnected() {}
  virtual void OnClose() {}
  virtual bool OnReadHeader(Msg* msg) {
    if (!msg) return false;
    return msg->DecodeHeader();
  }
  virtual void OnReadBody(Msg* msg) {
    if (!msg) return;
  }

 private:
  asio::io_service& io_service_;
  asio::ip::tcp::resolver resolver_;
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
    this->acceptor_.bind(endpoint);

    asio::socket_base::reuse_address option(true);
    this->acceptor_.set_option(option);
  }

  void DoAccept() {
    asio::ip::tcp::socket socket(acceptor_.get_io_service());
    acceptor_.async_accept(socket, [&socket, this](std::error_code ec) {
      if (!ec) {
        std::make_shared<Session>(std::move(socket))->DoReadHeader();
      }
      DoAccept();
    });
  }

 private:
  asio::ip::tcp::acceptor acceptor_;
};

int main() {
  std::cout << "Server." << std::endl;
  std::string ip = "127.0.0.1";
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
