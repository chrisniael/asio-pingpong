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
  void Write(const Msg& msg) {
    io_service_.post([=]() {
      bool write_in_progress = !write_msgs_.empty();
      write_msgs_.push_back(msg);
      if (!write_in_progress) {
        DoWrite();
      }
    });
  }

  void DoReadHeader() {
    asio::async_read(socket_,
                     asio::buffer(read_msg_.data(), Msg::kHeaderLength),
                     [=](std::error_code ec, std::size_t /*length*/) {
                       if (!ec && this->OnReadHeader(&read_msg_)) {
                         DoReadBody();
                       } else {
                         RecvError();
                       }
                     });
  }

  void DoReadBody() {
    asio::async_read(socket_,
                     asio::buffer(read_msg_.body(), read_msg_.body_length()),
                     [=](std::error_code ec, std::size_t /*length*/) {
                       if (!ec) {
                         this->OnReadBody(&read_msg_);
                         DoReadHeader();
                       } else {
                         RecvError();
                       }
                     });
  }
  void DoWrite() {
    asio::async_write(
        socket_,
        asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
        [=](std::error_code ec, std::size_t /*length*/) {
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

  virtual void RecvError() {}
  virtual void SendError() {}
  virtual void OnClose() {}
  virtual bool OnReadHeader(Msg* msg) {
    if (!msg) return false;
    return msg->DecodeHeader();
  }
  virtual void OnReadBody(Msg* msg) {
    if (!msg) return;

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

    this->Write(*msg);
  }

 private:
  asio::io_service& io_service_;
  asio::ip::tcp::resolver resolver_;
  asio::ip::tcp::socket socket_;
  Msg read_msg_;
  std::deque<Msg> write_msgs_;
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
            this->session_.DoReadHeader();
          } else {
            ConnError();
          }
        });
  }

  virtual void OnConnected() {
    std::cout << "Connected." << std::endl;
    Msg msg;
    msg.set_body_length(8);
    memset(msg.body(), 1, 8);
    session_.Write(msg);
  }
  virtual void ConnError() {}

 private:
  asio::ip::tcp::resolver resolver_;
  Session session_;
};

int main() {
  std::cout << "Client." << std::endl;
  std::string ip = "127.0.0.1";
  unsigned short port = 6789;

  try {
    asio::io_service io_service;
    Client client(io_service);
    client.Connect(ip, port);
    io_service.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
