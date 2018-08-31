/**
 * @file client.cpp
 * @brief client
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "asio.hpp"

static std::chrono::milliseconds time_from =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());

class IoServicePool {
 public:
  using IoService = asio::io_service;
  using Work = asio::io_service::work;
  using WorkPtr = std::unique_ptr<Work>;

  IoServicePool(std::size_t pool_size = std::thread::hardware_concurrency())
      : io_service_(pool_size), works_(pool_size), next_io_service_(0) {
    for (std::size_t i = 0; i < io_service_.size(); ++i) {
      works_[i] = std::unique_ptr<Work>(new Work(io_service_[i]));
    }

    for (std::size_t i = 0; i < io_service_.size(); ++i) {
      threads_.emplace_back([this, i]() { io_service_[i].run(); });
    }
  }
  IoServicePool(const IoServicePool&) = delete;
  ~IoServicePool() {}
  IoServicePool& operator=(const IoServicePool&) = delete;

  static IoServicePool& Instance() {
    static IoServicePool instance;
    return instance;
  }
  asio::io_service& GetIoService() {
    auto& service = io_service_[next_io_service_++ % io_service_.size()];
    return service;
  }
  void Join() {
    for (auto& t : threads_) {
      t.join();
    }
  }
  void Stop() {
    for (auto& io_service : this->io_service_) {
      io_service.stop();
    }
  }

 private:
  std::vector<IoService> io_service_;
  std::vector<WorkPtr> works_;
  std::vector<std::thread> threads_;
  std::atomic_size_t next_io_service_;
};

struct Buffer {
  enum { kMaxBufferSize = 16 };
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
    ++msg_count;

    if (msg_count > 10 * 10000) {
      std::chrono::milliseconds time_now =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch());
      uint64_t time_passed = (time_now - time_from).count();
      uint64_t pack_per_sec =
          static_cast<double>(msg_count) / time_passed * 1000;
      std::cout << "Recv msg, count=" << msg_count
                << ", time_passed=" << time_passed
                << ", pack/sec=" << pack_per_sec << std::endl;
      this->Close();
    } else {
      this->Write(buf);
    }
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
    std::list<Client> clients;
    for (int i = 0; i < 1; ++i) {
      clients.emplace_back(IoServicePool::Instance().GetIoService());
      clients.back().Connect(ip, port);
    }
    IoServicePool::Instance().Join();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }
}
