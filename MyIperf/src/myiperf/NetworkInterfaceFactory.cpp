#include "NetworkInterfaceFactory.h"

#ifdef _WIN32
#include "platform/WinIOCPNetworkInterface.h"
#else
#include "platform/LinuxAsyncNetworkInterface.h"
#endif

std::unique_ptr<NetworkInterface> createNetworkInterface() {
#ifdef _WIN32
  return std::make_unique<WinIOCPNetworkInterface>();
#else
  return std::make_unique<LinuxAsyncNetworkInterface>();
#endif
}
