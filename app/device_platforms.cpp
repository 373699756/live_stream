#include "device_platforms.h"

#include "infra/fs.h"

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
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace live_stream {
namespace {

constexpr const char *kResolvConfPath = "/etc/resolv.conf";

struct CpuTimes {
  uint64_t total = 0;
  uint64_t idle = 0;
};

std::string Trim(const std::string &value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' ||
          value[begin] == '\n')) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                         value[end - 1] == '\r' || value[end - 1] == '\n')) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string ReadFirstText(const std::vector<std::string> &paths) {
  for (const std::string &path : paths) {
    const std::string value = Trim(infra::File::ReadAll(path));
    if (!value.empty()) {
      return value;
    }
  }
  return std::string();
}

std::string ReadCpuInfoValue(const std::string &key) {
  const std::string content = infra::File::ReadAll("/proc/cpuinfo");
  if (content.empty()) {
    return std::string();
  }
  std::istringstream stream(content);
  std::string line;
  const std::string prefix = key + ":";
  while (std::getline(stream, line)) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
      return Trim(line.substr(prefix.size()));
    }
  }
  return std::string();
}

bool ReadCpuTimes(CpuTimes *times) {
  if (times == nullptr) {
    return false;
  }
  const std::string content = infra::File::ReadAll("/proc/stat");
  if (content.empty()) {
    return false;
  }
  std::istringstream stream(content);
  std::string cpu_label;
  uint64_t user = 0;
  uint64_t nice = 0;
  uint64_t system = 0;
  uint64_t idle = 0;
  uint64_t iowait = 0;
  uint64_t irq = 0;
  uint64_t softirq = 0;
  uint64_t steal = 0;
  stream >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >>
      softirq >> steal;
  if (!stream || cpu_label != "cpu") {
    return false;
  }
  times->idle = idle + iowait;
  times->total = user + nice + system + idle + iowait + irq + softirq + steal;
  return true;
}

uint32_t ReadMemoryUsagePercent() {
  const std::string content = infra::File::ReadAll("/proc/meminfo");
  if (content.empty()) {
    return 0;
  }
  std::istringstream stream(content);
  std::string key;
  int64_t value = 0;
  std::string unit;
  int64_t total_kb = 0;
  int64_t available_kb = 0;
  while (stream >> key >> value >> unit) {
    if (key == "MemTotal:") {
      total_kb = value;
    } else if (key == "MemAvailable:") {
      available_kb = value;
    }
  }
  if (total_kb <= 0 || available_kb < 0 || available_kb > total_kb) {
    return 0;
  }
  return static_cast<uint32_t>((total_kb - available_kb) * 100 / total_kb);
}

int32_t ReadTemperatureCelsius() {
  int64_t raw = 0;
  std::istringstream stream(
      infra::File::ReadAll("/sys/class/thermal/thermal_zone0/temp"));
  stream >> raw;
  if (!stream) {
    return 0;
  }
  if (raw > 1000) {
    raw /= 1000;
  }
  return static_cast<int32_t>(raw);
}

int64_t ReadUptimeMs() {
  std::istringstream stream(infra::File::ReadAll("/proc/uptime"));
  double uptime_sec = 0.0;
  stream >> uptime_sec;
  if (!stream) {
    return 0;
  }
  return static_cast<int64_t>(uptime_sec * 1000.0);
}

int64_t ReadSystemTimeMs() {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
    return 0;
  }
  return static_cast<int64_t>(now.tv_sec) * 1000LL +
         static_cast<int64_t>(now.tv_nsec / 1000000LL);
}

bool SetSystemTimeMsInternal(int64_t unix_time_ms) {
  if (unix_time_ms <= 0) {
    return false;
  }
  struct timespec value;
  value.tv_sec = static_cast<time_t>(unix_time_ms / 1000LL);
  value.tv_nsec = static_cast<long>((unix_time_ms % 1000LL) * 1000000LL);
  return clock_settime(CLOCK_REALTIME, &value) == 0;
}

int RunCommand(const std::vector<std::string> &argv) {
  if (argv.empty() || argv.front().empty()) {
    return -1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const std::string &item : argv) {
      args.push_back(const_cast<char *>(item.c_str()));
    }
    args.push_back(nullptr);
    execvp(args.front(), args.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return -1;
}

bool RunAny(const std::vector<std::vector<std::string>> &commands) {
  for (const std::vector<std::string> &command : commands) {
    if (RunCommand(command) == 0) {
      return true;
    }
  }
  return false;
}

bool IsExecutable(const std::string &path) {
  return !path.empty() && access(path.c_str(), X_OK) == 0;
}

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

std::string FindFactoryResetScript() {
  static const char *const kPaths[] = {
      "/usr/bin/factory_reset.sh", "/usr/sbin/factory_reset.sh",
      "/etc/init.d/factory_reset", "/usr/bin/factory_reset",
      "/usr/sbin/factory_reset",
  };
  for (const char *path : kPaths) {
    if (IsExecutable(path)) {
      return path;
    }
  }
  return std::string();
}

class LinuxSystemPlatform : public ISystemPlatform {
public:
  DeviceInfo GetDeviceInfo() override {
    DeviceInfo info;
    info.model = ReadFirstText({
        "/proc/device-tree/model",
        "/sys/firmware/devicetree/base/model",
    });
    info.serial_number = ReadFirstText({
        "/proc/device-tree/serial-number",
        "/sys/firmware/devicetree/base/serial-number",
    });
    if (info.serial_number.empty()) {
      info.serial_number = ReadCpuInfoValue("Serial");
    }
    info.firmware_version = ReadFirstText({
        "/etc/firmware_version",
        "/etc/version",
    });
    if (info.model.empty()) {
      info.model = "live_stream_ipc";
    }
    if (info.serial_number.empty()) {
      info.serial_number = "unknown";
    }
    if (info.firmware_version.empty()) {
      info.firmware_version = "0.1.0";
    }
    return info;
  }

  SystemStatus GetSystemStatus() override {
    SystemStatus status;
    status.cpu_usage_percent = ReadCpuUsagePercent();
    status.memory_usage_percent = ReadMemoryUsagePercent();
    status.temperature_celsius = ReadTemperatureCelsius();
    status.uptime_ms = ReadUptimeMs();
    status.healthy = true;
    return status;
  }

  SystemCapabilities GetCapabilities() override {
    SystemCapabilities caps;
    caps.supports_reboot =
        IsExecutable("/sbin/reboot") || IsExecutable("/bin/reboot") ||
        IsExecutable("/bin/busybox") || IsExecutable("/sbin/busybox");
    caps.supports_factory_reset = !FindFactoryResetScript().empty();
    caps.features.push_back("heartbeat");
    caps.features.push_back("system_status");
    caps.features.push_back("time_sync");
    caps.features.push_back("network_runtime");
    if (caps.supports_reboot) {
      caps.features.push_back("reboot");
    }
    if (caps.supports_factory_reset) {
      caps.features.push_back("factory_reset");
    }
    return caps;
  }

  bool Reboot() override {
    sync();
    if (reboot(RB_AUTOBOOT) == 0) {
      return true;
    }
    return RunAny({
        {"reboot"},
        {"/sbin/reboot"},
        {"busybox", "reboot"},
        {"/bin/busybox", "reboot"},
    });
  }

  bool FactoryReset() override {
    const std::string script = FindFactoryResetScript();
    if (script.empty()) {
      return false;
    }
    sync();
    return RunCommand({script}) == 0;
  }

private:
  uint32_t ReadCpuUsagePercent() {
    CpuTimes sample;
    if (!ReadCpuTimes(&sample)) {
      return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cpu_valid_) {
      last_cpu_ = sample;
      cpu_valid_ = true;
      return 0;
    }
    const uint64_t total_delta = sample.total - last_cpu_.total;
    const uint64_t idle_delta = sample.idle - last_cpu_.idle;
    last_cpu_ = sample;
    if (total_delta == 0 || idle_delta > total_delta) {
      return 0;
    }
    return static_cast<uint32_t>(((total_delta - idle_delta) * 100ULL) /
                                 total_delta);
  }

  std::mutex mutex_;
  CpuTimes last_cpu_;
  bool cpu_valid_ = false;
};

class LinuxTimePlatform : public ITimePlatform {
public:
  int64_t GetSystemTimeMs() override { return ReadSystemTimeMs(); }

  bool SetSystemTimeMs(int64_t unix_time_ms) override {
    return SetSystemTimeMsInternal(unix_time_ms);
  }

  bool SyncNtp(const std::vector<std::string> &servers,
               int64_t *synced_time_ms) override {
    if (synced_time_ms == nullptr || servers.empty()) {
      return false;
    }
    std::vector<std::string> ntpd = {"ntpd", "-n", "-q"};
    std::vector<std::string> busybox_ntpd = {"busybox", "ntpd", "-n", "-q"};
    std::vector<std::string> ntpdate = {"ntpdate", "-b"};
    for (const std::string &server : servers) {
      ntpd.push_back("-p");
      ntpd.push_back(server);
      busybox_ntpd.push_back("-p");
      busybox_ntpd.push_back(server);
      ntpdate.push_back(server);
    }
    if (!RunAny({ntpd, busybox_ntpd, ntpdate})) {
      return false;
    }
    *synced_time_ms = ReadSystemTimeMs();
    return *synced_time_ms > 0;
  }
};

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

} // namespace

std::unique_ptr<ISystemPlatform> CreateLinuxSystemPlatform() {
  return std::unique_ptr<ISystemPlatform>(new LinuxSystemPlatform());
}

std::unique_ptr<ITimePlatform> CreateLinuxTimePlatform() {
  return std::unique_ptr<ITimePlatform>(new LinuxTimePlatform());
}

std::unique_ptr<INetworkPlatform>
CreateLinuxNetworkPlatform(const std::string &default_ifname) {
  return std::unique_ptr<INetworkPlatform>(
      new LinuxNetworkPlatform(default_ifname));
}

} // namespace live_stream
