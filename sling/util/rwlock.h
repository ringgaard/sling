// Copyright 2025 Ringgaard Research ApS
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SLING_UTIL_RWLOCK_H_
#define SLING_UTIL_RWLOCK_H_

#include <pthread.h>

namespace sling {

// Read/write locking with shared and exclusive locking.
class RWLock {
 public:
  RWLock() { pthread_rwlock_init(&lock_, nullptr); }
  ~RWLock() { pthread_rwlock_destroy(&lock_); }

  // Wait for shared lock.
  void LockShared() { pthread_rwlock_rdlock(&lock_); }

  // Wait for exclusive lock.
  void LockExclusive() { pthread_rwlock_wrlock(&lock_); }

  // Release lock.
  void Unlock() { pthread_rwlock_unlock(&lock_); }

 private:
  pthread_rwlock_t lock_;
};

// Shared lock.
class SharedLock {
 public:
  // Constructor that acquires shared lock.
  explicit SharedLock(RWLock *lock) : lock_(lock) { lock_->LockShared(); }

  // Destructor that releases lock.
  ~SharedLock() { lock_->Unlock(); }

 private:
  // Lock for guard.
  RWLock *lock_;
};

// Exclusive lock.
class ExclusiveLock {
 public:
  // Constructor that acquires exclusive.
  explicit ExclusiveLock(RWLock *lock) : lock_(lock) { lock_->LockExclusive(); }

  // Destructor that releases lock.
  ~ExclusiveLock() { lock_->Unlock(); }

 private:
  // Lock for guard.
  RWLock *lock_;
};

}  // namespace sling

#endif  // SLING_UTIL_RWLOCK_H_
