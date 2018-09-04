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
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "asio.hpp"
#include "asio/steady_timer.hpp"

std::chrono::milliseconds time_from =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
std::atomic_ullong pack_num{0};

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
  enum { kMaxBufferSize = 16 };
  char buffer[kMaxBufferSize];
};

class Session {
 public:
  Session(asio::io_service& io_service)
      : io_service_(io_service), resolver_(io_service), socket_(io_service) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  virtual ~Session() {}

  asio::io_service& get_io_service() { return this->io_service_; }
  asio::ip::tcp::socket& get_socket() { return this->socket_; }

  void Close() {
    this->socket_.shutdown(asio::ip::tcp::socket::shutdown_send);
    this->socket_.close();
    this->OnClose();
  }
  void Write(const Buffer& buf) {
    bool write_in_progress = !write_bufs_.empty();
    write_bufs_.push_back(buf);
    if (!write_in_progress) {
      DoWrite();
    }
  }

  void DoRead() {
    asio::async_read(
        socket_, asio::buffer(this->read_buf_.buffer, Buffer::kMaxBufferSize),
        [=](std::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            this->OnRead(this->read_buf_);
            DoRead();
          } else {
            RecvError(ec);
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
            SendError(ec);
          }
        });
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
  virtual void OnClose() {}
  virtual void OnRead(const Buffer& buf) {
    ++pack_num;
    this->Write(buf);
  }

 private:
  asio::io_service& io_service_;
  asio::ip::tcp::resolver resolver_;
  asio::ip::tcp::socket socket_;
  Buffer read_buf_;
  std::deque<Buffer> write_bufs_;
};

class Client : public std::enable_shared_from_this<Client> {
 public:
  Client(asio::io_service& io_service)
      : resolver_(io_service), session_(io_service) {}

  void Connect(const std::string& ip, const unsigned short& port) {
    auto endpoint_iterator =
        this->resolver_.resolve({ip, std::to_string(port)});
    this->DoConnect(endpoint_iterator);
  }

  void DoConnect(asio::ip::tcp::resolver::iterator endpoint_iterator) {
    auto self = this->shared_from_this();
    asio::async_connect(
        this->session_.get_socket(), endpoint_iterator,
        [this, self](std::error_code ec, asio::ip::tcp::resolver::iterator) {
          if (!ec) {
            this->OnConnected();
            this->session_.DoRead();
          } else {
            ConnError();
          }
        });
  }

  virtual void OnConnected() {
    Buffer buf;
    memset(buf.buffer, 1, Buffer::kMaxBufferSize);
    session_.Write(buf);
  }

  virtual void ConnError() { std::cerr << "ConnError." << std::endl; }

  void Close() {
    auto self = this->shared_from_this();
    this->session_.get_io_service().post(
        [this, self]() { this->session_.Close(); });
  }

 private:
  asio::ip::tcp::resolver resolver_;
  Session session_;
};

int main(int argc, char* argv[]) {
  if (argc != 5) {
    std::cerr << "Usage: client <host> <port> <num> <time>\n";
    return 1;
  }
  std::cout << "Client start." << std::endl;

  std::string ip = argv[1];
  unsigned short port = std::stoi(argv[2]);
  unsigned int client_num = std::stoi(argv[3]);
  unsigned int time_seconds = std::stoi(argv[4]);

  try {
    IoServicePool::Instance().Init();
    asio::io_service io_service;
    std::vector<std::shared_ptr<Client>> clients;
    for (unsigned int i = 0; i < client_num; ++i) {
      clients.emplace_back(
          new Client(IoServicePool::Instance().GetIoService()));
      clients.back()->Connect(ip, port);
    }
    asio::steady_timer timer(io_service);
    std::chrono::seconds time_long{std::stoi(argv[4])};
    timer.expires_from_now(time_long);
    timer.async_wait(
        [](const asio::error_code&) { IoServicePool::Instance().Stop(); });

    io_service.run();
    IoServicePool::Instance().Join();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
  }

  std::chrono::milliseconds time_now =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch());
  uint64_t time_passed = (time_now - time_from).count();
  uint64_t pack_per_sec = static_cast<double>(pack_num) / time_passed * 1000;
  std::cout << "Recv msg, pack_num=" << pack_num
            << ", time_passed=" << time_passed << ", pack/sec=" << pack_per_sec
            << std::endl;
}
