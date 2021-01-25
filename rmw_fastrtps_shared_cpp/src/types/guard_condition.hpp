// Copyright 2016-2018 Proyectos y Sistemas de Mantenimiento SL (eProsima).
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

#ifndef TYPES__GUARD_CONDITION_HPP_
#define TYPES__GUARD_CONDITION_HPP_

#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <utility>

#include "rcpputils/thread_safety_annotations.hpp"

#include "rmw/listener_event_types.h"

class GuardCondition
{
public:
  GuardCondition()
  : hasTriggered_(false),
    conditionMutex_(nullptr), conditionVariable_(nullptr) {}

  void
  trigger()
  {
    std::unique_lock<std::mutex> lock_mutex(listener_callback_mutex_);

    if(listener_callback_)
    {
      listener_callback_(user_data_, { waitable_handle_, WAITABLE_EVENT });
    } else {
      std::lock_guard<std::mutex> lock(internalMutex_);

      if (conditionMutex_ != nullptr) {
        std::unique_lock<std::mutex> clock(*conditionMutex_);
        // the change to hasTriggered_ needs to be mutually exclusive with
        // rmw_wait() which checks hasTriggered() and decides if wait() needs to
        // be called
        hasTriggered_ = true;
        clock.unlock();
        conditionVariable_->notify_one();
      } else {
        hasTriggered_ = true;
      }

      unread_count_++;
    }
  }

  void
  attachCondition(std::mutex * conditionMutex, std::condition_variable * conditionVariable)
  {
    std::lock_guard<std::mutex> lock(internalMutex_);
    conditionMutex_ = conditionMutex;
    conditionVariable_ = conditionVariable;
  }

  void
  detachCondition()
  {
    std::lock_guard<std::mutex> lock(internalMutex_);
    conditionMutex_ = nullptr;
    conditionVariable_ = nullptr;
  }

  bool
  hasTriggered()
  {
    return hasTriggered_;
  }

  bool
  getHasTriggered()
  {
    return hasTriggered_.exchange(false);
  }

  // Provide handlers to perform an action when a
  // new event from this listener has ocurred
  void
  guardConditionSetExecutorCallback(
    const void * user_data,
    rmw_listener_cb_t callback,
    const void * waitable_handle,
    bool use_previous_events)
  {
    std::unique_lock<std::mutex> lock_mutex(listener_callback_mutex_);

    if(user_data && waitable_handle && callback)
    {
      user_data_ = user_data;
      listener_callback_ = callback;
      waitable_handle_ = waitable_handle;
    } else {
      // Unset callback: If any of the pointers is NULL, do not use callback.
      user_data_ = nullptr;
      listener_callback_ = nullptr;
      waitable_handle_ = nullptr;
      return;
    }

    if (use_previous_events) {
      // Push events arrived before setting the executor's callback
      for(uint64_t i = 0; i < unread_count_; i++) {
        listener_callback_(user_data_, { waitable_handle_, WAITABLE_EVENT });
      }
    }

    // Reset unread count
    unread_count_ = 0;
  }

private:
  std::mutex internalMutex_;
  std::atomic_bool hasTriggered_;
  std::mutex * conditionMutex_ RCPPUTILS_TSA_GUARDED_BY(internalMutex_);
  std::condition_variable * conditionVariable_ RCPPUTILS_TSA_GUARDED_BY(internalMutex_);

  rmw_listener_cb_t listener_callback_{nullptr};
  const void * waitable_handle_{nullptr};
  const void * user_data_{nullptr};
  std::mutex listener_callback_mutex_;
  uint64_t unread_count_ = 0;
};

#endif  // TYPES__GUARD_CONDITION_HPP_
