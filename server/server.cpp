/**
 * @file server.cpp
 * @brief server
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <atomic>
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
    next_io_service_ = 0;

    for (size_t i = 0; i < pool_size; ++i) {
      io_services_.emplace_back(new asio::io_service);
    }

    for (size_t i = 0; i < io_services_.size(); ++i) {
      works_.emplace_back(new Work(*(io_services_[i])));
    }

    for (size_t i = 0; i < io_services_.size(); ++i) {
      threads_.emplace_back([this, i]() { io_services_[i]->run(); });
    }
  }

  static IoServicePool& Instance() {
    static IoServicePool instance;
    return instance;
  }

  asio::io_service& GetIoService() {
    auto& service = io_services_[next_io_service_++ % io_services_.size()];
    return *service.get();
  }

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

  void Stop() {
    for (auto& io_service : this->io_services_) {
      io_service->stop();
    }
  }

 private:
  std::vector<std::unique_ptr<asio::io_service>> io_services_;
  std::vector<WorkPtr> works_;
  std::vector<std::thread> threads_;
  std::atomic_size_t next_io_service_;
};

struct Buffer {
  enum { kMaxBufferSize = 1 };
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
  void Write(const Buffer& buf) {
    auto self = this->shared_from_this();
    io_service_.dispatch([this, self, buf]() {
      bool write_in_progress = !write_bufs_.empty();
      write_bufs_.push_back(std::move(buf));
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
                         RecvError(ec);
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
            SendError(ec);
          }
        });
  }

  virtual void RecvError(std::error_code ec) {
    if (ec != asio::error::connection_reset) {
      std::cerr << "RecvError, value=" << ec.value()
                << ", what=" << ec.message() << std::endl;
    }
  }
  virtual void SendError(std::error_code ec) {
    std::cerr << "SendError, value=" << ec.value() << ", what=" << ec.message()
              << std::endl;
  }
  virtual void OnClose() { std::cout << "OnClose." << std::endl; }
  virtual void OnRead(const Buffer& buf) { this->Write(buf); }

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
    asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(ip), port);
    this->acceptor_.open(asio::ip::tcp::v4());
    this->acceptor_.set_option(asio::socket_base::reuse_address(true));
    this->acceptor_.set_option(asio::ip::tcp::no_delay(true));
    this->acceptor_.non_blocking(true);

    // asio::socket_base::send_buffer_size SNDBUF(16);
    // asio::socket_base::receive_buffer_size RCVBUF(16);
    // this->acceptor_.set_option(SNDBUF);
    // this->acceptor_.set_option(RCVBUF);

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
