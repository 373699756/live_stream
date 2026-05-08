#include "device_platforms.h"

#include "infra/fs.h"
#include "linux_platform_common.h"

#include <arpa/inet.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

using linux_platform::RunAny;
using linux_platform::Trim;

constexpr const char *kResolvConfPath = "/etc/resolv.conf";

std::string NetmaskFromPrefix(uint8_t prefix_length) {
  if (prefix_length == 0 || prefix_length > 32) {
    return std::string();
  }
  const uint32_t mask =
      prefix_length == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix_length));
  return std::to_string((mask >> 24) & 0xff) + "." +
         std::to_string((mask >> 16) & 0xff) + "." +
         std::to_string((mask >> 8) & 0xff) + "." + std::to_string(mask & 0xff);
}

std::string HexGatewayToIpv4(const std::string &hex_value) {
  if (hex_value.size() != 8) {
    return std::string();
  }
  char *end = nullptr;
  const unsigned long raw = std::strtoul(hex_value.c_str(), &end, 16);
  if (end == nullptr || *end != '\0') {
    return std::string();
  }
  return std::to_string(raw & 0xffUL) + "." +
         std::to_string((raw >> 8) & 0xffUL) + "." +
         std::to_string((raw >> 16) & 0xffUL) + "." +
         std::to_string((raw >> 24) & 0xffUL);
}

std::vector<std::string> ReadDnsServers() {
  std::vector<std::string> servers;
  std::istringstream stream(infra::File::ReadAll(kResolvConfPath));
  std::string token;
  while (stream >> token) {
    if (token != "nameserver") {
      std::string ignored;
      std::getline(stream, ignored);
      continue;
    }
    std::string server;
    stream >> server;
    if (!server.empty()) {
      servers.push_back(server);
    }
  }
  return servers;
}

bool WriteDnsServers(const std::vector<std::string> &dns_servers) {
  std::string content;
  for (const std::string &server : dns_servers) {
    content += "nameserver " + server + "\n";
  }
  return infra::File::WriteAll(kResolvConfPath, content);
}

std::string ReadDefaultGateway(const std::string &ifname) {
  std::istringstream stream(infra::File::ReadAll("/proc/net/route"));
  std::string line;
  if (!std::getline(stream, line)) {
    return std::string();
  }
  while (std::getline(stream, line)) {
    std::istringstream row(line);
    std::string route_ifname;
    std::string destination;
    std::string gateway;
    row >> route_ifname >> destination >> gateway;
    if (!row) {
      continue;
    }
    if (route_ifname == ifname && destination == "00000000") {
      return HexGatewayToIpv4(gateway);
    }
  }
  return std::string();
}

bool LoadIfreq(const std::string &ifname, struct ifreq *request) {
  if (request == nullptr || ifname.empty() || ifname.size() >= IFNAMSIZ) {
    return false;
  }
  std::memset(request, 0, sizeof(*request));
  std::snprintf(request->ifr_name, IFNAMSIZ, "%s", ifname.c_str());
  return true;
}

bool ReadIfFlags(const std::string &ifname, short *flags) {
  if (flags == nullptr) {
    return false;
  }
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return false;
  }
  struct ifreq request;
  const bool ready =
      LoadIfreq(ifname, &request) && ioctl(fd, SIOCGIFFLAGS, &request) == 0;
  if (ready) {
    *flags = request.ifr_flags;
  }
  close(fd);
  return ready;
}

std::string SockaddrToIpv4(const struct sockaddr_in &address) {
  char buffer[INET_ADDRSTRLEN] = {0};
  if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) ==
      nullptr) {
    return std::string();
  }
  return buffer;
}

bool ReadIpv4Address(const std::string &ifname, int request_code,
                     std::string *value) {
  if (value == nullptr) {
    return false;
  }
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return false;
  }
  struct ifreq request;
  const bool ready =
      LoadIfreq(ifname, &request) && ioctl(fd, request_code, &request) == 0;
  if (ready) {
    *value = SockaddrToIpv4(
        *reinterpret_cast<struct sockaddr_in *>(&request.ifr_addr));
  }
  close(fd);
  return ready;
}

bool ReadPrefixLength(const std::string &ifname, uint8_t *prefix_length) {
  if (prefix_length == nullptr) {
    return false;
  }
  std::string netmask;
  if (!ReadIpv4Address(ifname, SIOCGIFNETMASK, &netmask)) {
    return false;
  }
  struct in_addr address;
  if (inet_pton(AF_INET, netmask.c_str(), &address) != 1) {
    return false;
  }
  uint32_t mask = ntohl(address.s_addr);
  uint8_t bits = 0;
  while ((mask & 0x80000000u) != 0) {
    ++bits;
    mask <<= 1;
  }
  if (mask != 0) {
    return false;
  }
  *prefix_length = bits;
  return true;
}

std::string DhcpPidPath(const std::string &ifname) {
  return std::string("/var/run/udhcpc.") + ifname + ".pid";
}

bool StopDhcpPid(const std::string &ifname) {
  const std::string pid_text = Trim(infra::File::ReadAll(DhcpPidPath(ifname)));
  if (pid_text.empty()) {
    return false;
  }
  char *end = nullptr;
  const long pid_value = std::strtol(pid_text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || pid_value <= 0) {
    return false;
  }
  const bool ok = kill(static_cast<pid_t>(pid_value), SIGTERM) == 0;
  if (ok) {
    static_cast<void>(infra::File::Remove(DhcpPidPath(ifname)));
  }
  return ok;
}

bool HasDhcpPid(const std::string &ifname) {
  return infra::File::Exists(DhcpPidPath(ifname));
}

class LinuxNetworkPlatform : public INetworkPlatform {
public:
  explicit LinuxNetworkPlatform(std::string default_ifname)
      : default_ifname_(std::move(default_ifname)) {}

  std::vector<std::string> ListInterfaces() override {
    std::vector<std::string> ifnames;
    DIR *directory = opendir("/sys/class/net");
    if (directory != nullptr) {
      struct dirent *entry = nullptr;
      while ((entry = readdir(directory)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
          continue;
        }
        ifnames.push_back(name);
      }
      closedir(directory);
    }
    if (ifnames.empty() && !default_ifname_.empty()) {
      ifnames.push_back(default_ifname_);
    }
    return ifnames;
  }

  NetworkInterfaceStatus
  GetInterfaceStatus(const std::string &ifname) override {
    NetworkInterfaceStatus status;
    status.ifname = ifname;
    status.gateway = ReadDefaultGateway(ifname);
    status.dns_servers = ReadDnsServers();
    status.mac_address =
        Trim(infra::File::ReadAll("/sys/class/net/" + ifname + "/address"));
    status.last_ok = infra::Path::Exists("/sys/class/net/" + ifname);

    short flags = 0;
    if (ReadIfFlags(ifname, &flags)) {
      status.enabled = (flags & IFF_UP) != 0;
      status.link_up = (flags & IFF_RUNNING) != 0;
    }
    std::string ipv4_address;
    if (ReadIpv4Address(ifname, SIOCGIFADDR, &ipv4_address)) {
      status.ipv4_address = ipv4_address;
    }
    uint8_t prefix_length = 0;
    if (ReadPrefixLength(ifname, &prefix_length)) {
      status.prefix_length = prefix_length;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto iter = dhcp_enabled_.find(ifname);
      if (iter != dhcp_enabled_.end()) {
        status.dhcp_enabled = iter->second;
      } else {
        status.dhcp_enabled = HasDhcpPid(ifname);
      }
    }
    return status;
  }

  bool SetInterfaceEnabled(const std::string &ifname, bool enabled) override {
    const char *state = enabled ? "up" : "down";
    if (RunAny({
            {"ip", "link", "set", "dev", ifname, state},
            {"busybox", "ip", "link", "set", "dev", ifname, state},
            {"ifconfig", ifname, state},
            {"busybox", "ifconfig", ifname, state},
        })) {
      if (!enabled) {
        static_cast<void>(StopDhcp(ifname));
      }
      return true;
    }
    return false;
  }

  bool ApplyStaticAddress(const NetworkInterfaceConfig &config) override {
    const std::string cidr =
        config.ipv4_address + "/" + std::to_string(config.prefix_length);
    const std::string netmask = NetmaskFromPrefix(config.prefix_length);
    bool ok = RunAny({
        {"ip", "addr", "flush", "dev", config.ifname},
        {"busybox", "ip", "addr", "flush", "dev", config.ifname},
    });
    if (ok) {
      ok = RunAny({
          {"ip", "addr", "add", cidr, "dev", config.ifname},
          {"busybox", "ip", "addr", "add", cidr, "dev", config.ifname},
      });
    }
    if (!ok) {
      ok = RunAny({
          {"ifconfig", config.ifname, config.ipv4_address, "netmask", netmask,
           "up"},
          {"busybox", "ifconfig", config.ifname, config.ipv4_address, "netmask",
           netmask, "up"},
      });
    }
    if (ok) {
      std::lock_guard<std::mutex> lock(mutex_);
      dhcp_enabled_[config.ifname] = false;
    }
    return ok;
  }

  bool StartDhcp(const std::string &ifname) override {
    const std::string pid_path = DhcpPidPath(ifname);
    static_cast<void>(StopDhcpPid(ifname));
    const bool ok = RunAny({
        {"udhcpc", "-q", "-n", "-t", "5", "-T", "3", "-R", "-p", pid_path, "-i",
         ifname},
        {"busybox", "udhcpc", "-q", "-n", "-t", "5", "-T", "3", "-R", "-p",
         pid_path, "-i", ifname},
        {"dhclient", "-1", "-pf", pid_path, ifname},
    });
    if (ok) {
      std::lock_guard<std::mutex> lock(mutex_);
      dhcp_enabled_[ifname] = true;
    }
    return ok;
  }

  bool StopDhcp(const std::string &ifname) override {
    const bool pid_stopped = StopDhcpPid(ifname);
    const bool released = RunAny({{"dhclient", "-r", ifname}});
    {
      std::lock_guard<std::mutex> lock(mutex_);
      dhcp_enabled_[ifname] = false;
    }
    return pid_stopped || released || !HasDhcpPid(ifname);
  }

  bool SetGateway(const std::string &ifname,
                  const std::string &gateway) override {
    if (gateway.empty()) {
      if (ReadDefaultGateway(ifname).empty()) {
        return true;
      }
      return RunAny({
          {"ip", "route", "del", "default", "dev", ifname},
          {"busybox", "ip", "route", "del", "default", "dev", ifname},
          {"route", "del", "default", ifname},
          {"busybox", "route", "del", "default", ifname},
      });
    }
    return RunAny({
        {"ip", "route", "replace", "default", "via", gateway, "dev", ifname},
        {"busybox", "ip", "route", "replace", "default", "via", gateway, "dev",
         ifname},
        {"route", "add", "default", "gw", gateway, ifname},
        {"busybox", "route", "add", "default", "gw", gateway, ifname},
    });
  }

  bool SetDnsServers(const std::vector<std::string> &dns_servers) override {
    return WriteDnsServers(dns_servers);
  }

  bool
  RollbackInterface(const NetworkInterfaceConfig &previous_config) override {
    if (!SetInterfaceEnabled(previous_config.ifname, previous_config.enabled)) {
      return false;
    }
    if (!previous_config.enabled) {
      return true;
    }
    bool ok = true;
    if (previous_config.address_mode == NetworkAddressMode::kDhcp) {
      ok = StartDhcp(previous_config.ifname);
    } else {
      ok = StopDhcp(previous_config.ifname) &&
           ApplyStaticAddress(previous_config);
    }
    return ok && SetGateway(previous_config.ifname, previous_config.gateway) &&
           SetDnsServers(previous_config.dns_servers);
  }

private:
  std::string default_ifname_;
  std::mutex mutex_;
  std::map<std::string, bool> dhcp_enabled_;
};

}  // namespace

std::unique_ptr<INetworkPlatform>
CreateLinuxNetworkPlatform(const std::string &default_ifname) {
  return std::unique_ptr<INetworkPlatform>(
      new LinuxNetworkPlatform(default_ifname));
}

}  // namespace live_stream
