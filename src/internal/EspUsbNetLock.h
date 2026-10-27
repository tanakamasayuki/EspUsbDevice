#pragma once
namespace espusb { namespace internal {
struct NetGuard {
  NetGuard(); ~NetGuard();
  NetGuard(const NetGuard &) = delete;
  NetGuard &operator=(const NetGuard &) = delete;
};
} }
