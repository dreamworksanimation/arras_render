// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#ifndef NOTIFIED_VALUE_H_
#define NOTIFIED_VALUE_H_

#include <condition_variable>
#include <mutex>

template <class T>
class NotifiedValue {
  public:
    NotifiedValue(T value) : mValue(value) {};

    //
    // set the value if it has changed and notify the waiters to wake up
    //
    void set(T newValue) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mValue != newValue) {
            mValue = newValue;
            mCondition.notify_all();
        }
    }

    NotifiedValue& operator = (T newValue) {
        set(newValue);
        return *this;
    }

    operator T () const {
        std::unique_lock<std::mutex> lock(mMutex);
        return mValue;
    }
       

    //
    // get the current value
    //
    T get() const {
        // the value will be potentially be stale as soon as the function returns but this
        // lock assures that no changes from before the call are missed by causing
        // appropriate memory barriers to occur and to keep the read from being
        // optimized out
        std::unique_lock<std::mutex> lock(mMutex);
        return mValue;
    }

    //
    // wait for a value other than the one passed in
    //
    // It's possible it became different and went back to the same value before the
    // first time this function locks but the caller isn't really interested in
    // transient changes anyway. This wouldn't be good for reliably logging all changes
    // though.
    T getDifferent(T oldValue) const {
        std::unique_lock<std::mutex> lock(mMutex);
        while (mValue == oldValue) {
            mCondition.wait(lock);
        }
        return mValue;
    }

    T getWhenGreater(T value) const {
        std::unique_lock<std::mutex> lock(mMutex);

        // this is written as ! > rather than <= because a NaN
        // will return true on ! > but will return false on <=
        while (!(mValue > value)) {
            mCondition.wait(lock);
        }
        return mValue;
    }

    T getWhenGreaterOrEqualTo(T value) const {
        std::unique_lock<std::mutex> lock(mMutex);

        // this is written as ! >= rather than < because a NaN
        // will return true on ! >= but will return false on <
        while (!(mValue >= value)) {
            mCondition.wait(lock);
        }
        return mValue;
    }

  private:
    mutable std::mutex mMutex;
    mutable std::condition_variable mCondition;
    T mValue;
};

#endif // NOTIFIED_VALUE_H_
