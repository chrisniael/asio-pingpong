/**
 * @file msg.h
 * @brief msg
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */
#ifndef ASIO_PINGPONG_COMMON_MSG_H_
#define ASIO_PINGPONG_COMMON_MSG_H_

#include <cstdint>
#include <cstring>

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

#endif
