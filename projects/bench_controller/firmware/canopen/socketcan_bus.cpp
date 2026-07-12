#include "socketcan_bus.hpp"

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace bench {

SocketCanBus::~SocketCanBus() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

bool SocketCanBus::Open(const std::string& ifname) {
  fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd_ < 0) {
    return false;
  }

  ifreq ifr{};
  ::strncpy(ifr.ifr_name, ifname.c_str(), sizeof(ifr.ifr_name) - 1);
  if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  const int flags = ::fcntl(fd_, F_GETFL, 0);
  ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
  return true;
}

bool SocketCanBus::Send(const CanFrame& frame) {
  if (fd_ < 0) return false;
  can_frame raw{};
  raw.can_id = frame.id & CAN_SFF_MASK;
  raw.can_dlc = frame.dlc;
  ::memcpy(raw.data, frame.data, 8);
  return ::write(fd_, &raw, sizeof(raw)) == sizeof(raw);
}

std::optional<CanFrame> SocketCanBus::Poll() {
  if (fd_ < 0) return std::nullopt;
  can_frame raw{};
  const ssize_t n = ::read(fd_, &raw, sizeof(raw));
  if (n != static_cast<ssize_t>(sizeof(raw))) {
    return std::nullopt;
  }
  if ((raw.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0) {
    return std::nullopt;  // расширенные/RTR/ошибочные кадры не используем
  }
  CanFrame frame{};
  frame.id = raw.can_id & CAN_SFF_MASK;
  frame.dlc = raw.can_dlc > 8 ? 8 : raw.can_dlc;
  ::memcpy(frame.data, raw.data, 8);
  return frame;
}

}  // namespace bench
