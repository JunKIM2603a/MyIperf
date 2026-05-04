#pragma once

#include <memory>

class NetworkInterface;

std::unique_ptr<NetworkInterface> createNetworkInterface();
