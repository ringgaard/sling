#ifndef SLING_UTIL_MUTEX_H_
#define SLING_UTIL_MUTEX_H_

#include <mutex>

namespace sling {

// Basic mutex wrapper around a std::mutex.
class Mutex : public std::mutex {
 public:
  void Lock() { lock(); }
  void Unlock() { unlock(); }
  void TryLock() { try_lock(); }
};

// Lock guard.
class MutexLock {
 public:
  explicit MutexLock(Mutex *lock) : lock_(lock) { lock_->Lock(); }
  ~MutexLock() { lock_->Unlock(); }

 private:
  Mutex *lock_;
};

}  // namespace sling

#endif  // SLING_UTIL_MUTEX_H_

