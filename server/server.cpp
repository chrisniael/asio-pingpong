/**
 * @file server.cpp
 * @brief server
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "asio.hpp"

class IoServicePool {
 public:
  using Work = asio::io_service::work;
  using WorkPtr = std::unique_ptr<Work>;

  IoServicePool() = default;
  IoServicePool(const IoServicePool&) = delete;
  ~IoServicePool() {}
  IoServicePool& operator=(const IoServicePool&) = delete;

  void Init(std::size_t pool_size = std::thread::hardware_concurrency()) {
    for (size_t i = 0; i < pool_size; ++i) {
      works_.emplace_back(new Work(this->io_service_));
    }

    for (size_t i = 0; i < pool_size; ++i) {
      threads_.emplace_back([this, i]() { io_service_.run(); });
    }
  }

  static IoServicePool& Instance() {
    static IoServicePool instance;
    return instance;
  }

  asio::io_service& GetIoService() { return this->io_service_; }

  void Join() {
    for (auto& t : threads_) {
      t.join();
    }
  }

  void ClearWork() {
    for (auto& work : this->works_) {
      work.reset();
    }
  }

  void Stop() { io_service_.stop(); }

 private:
  asio::io_service io_service_;
  std::vector<WorkPtr> works_;
  std::vector<std::thread> threads_;
};

struct Buffer {
  enum { kMaxBufferSize = 16 };
  std::vector<char> buffer{kMaxBufferSize};
};

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(asio::io_service& io_service)
      : io_service_(io_service), strand_(io_service), socket_(io_service) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  virtual ~Session() {}

  asio::ip::tcp::socket& get_socket() { return this->socket_; }

  void Close() {
    auto self = this->shared_from_this();
    this->io_service_.post(this->strand_.wrap([this, self]() {
      this->socket_.shutdown(asio::ip::tcp::socket::shutdown_send);
      this->socket_.close();
      this->OnClose();
    }));
  }

  void DoRead() {
    auto self = this->shared_from_this();
    socket_.async_read_some(
        asio::buffer(read_buf_.buffer, Buffer::kMaxBufferSize),
        this->strand_.wrap([this, self](const asio::error_code& ec,
                                        std::size_t bytes_transferred) {
          if (!ec) {
            this->OnRead(read_buf_, bytes_transferred);
            // DoRead();
          } else {
            RecvError(ec);
          }
        }));
  }

  virtual void RecvError(std::error_code ec) {
    if (ec != asio::error::eof && ec != asio::error::connection_reset) {
      std::cerr << "RecvError, value=" << ec.value()
                << ", what=" << ec.message() << std::endl;
    }
  }
  virtual void SendError(std::error_code ec) {
    std::cerr << "SendError, value=" << ec.value() << ", what=" << ec.message()
              << std::endl;
  }
  virtual void OnClose() { std::cout << "OnClose." << std::endl; }
  virtual void OnRead(Buffer& buf, std::size_t bytes) {
    this->write_bufs_.buffer.swap(buf.buffer);
    auto self = this->shared_from_this();
    asio::async_write(socket_, asio::buffer(write_bufs_.buffer, bytes),
                      this->strand_.wrap([this, self](std::error_code ec,
                                                      std::size_t /*length*/) {
                        if (!ec) {
                          DoRead();
                        } else {
                          SendError(ec);
                        }
                      }));
  }

 private:
  asio::io_service& io_service_;
  asio::io_service::strand strand_;
  asio::ip::tcp::socket socket_;
  Buffer read_buf_;
  Buffer write_bufs_;
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
    auto session =
        std::make_shared<Session>(IoServicePool::Instance().GetIoService());
    acceptor_.async_accept(session->get_socket(), [=](std::error_code ec) {
      if (!ec) {
        session->DoRead();
        DoAccept();
      } else {
        std::cerr << "AcceptError, value=" << ec.value()
                  << ", what=" << ec.message() << std::endl;
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
    IoServicePool::Instance().Init();
    asio::io_service io_service;
    Server server(io_service, ip, port);
    server.DoAccept();
    io_service.run();
    IoServicePool::Instance().Join();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
