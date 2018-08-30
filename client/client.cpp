/**
 * @file client.cpp
 * @brief client
 * @author shenyu, shenyu@shenyu.me
 * @version
 * @date 2018-08-30
 */

#include <iostream>

#include "asio.hpp"

int main() {
  std::cout << "Client." << std::endl;
  asio::io_service io;
  io.run();
}
