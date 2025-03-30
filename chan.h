#pragma once
#include <memory>
#include <atomic>
#include <functional>
#include <deque>
#include <list>
#include <condition_variable>
#include <mutex>
#include "util.h"

/**
  multicast queue

  Producers use `send` or `send_ptr` methods to send the objects.
  Consumers register by calling the `auto r = chan.create_receiver(callback)` and then
  calling `r->run()` from the handler thread. `run` blocks until the channel is closed
  or `chan.remove_receiver(r)` is called

 */
template <typename T> class mcast_chan {
    enum class state { running, closed, stopped };

public:
    using callback_t = std::function<void(const T &)>;
    using obj_ptr_t = std::shared_ptr<T>;
    ~mcast_chan() {
        close();
    }
    mcast_chan() = default;
    mcast_chan(const mcast_chan &) = delete;
    /// Send an object (managed by shared_ptr) to the registered receivers
    void send_ptr(const obj_ptr_t &);
    /// send a newly constructed T(args) to the receivers
    template <typename... U> void send(U &&...args);
    /// close the channel and make `receiver->run()` return
    /// if `abandon_q` is `true`, outstanding queue items are discarded
    void close(bool abandon_q = false);
    /// return number of currently registered receivers
    size_t receiver_count() const {
        std::scoped_lock l{ r_lock_ };
        return receivers.size();
    }
    /// create new receiver end of channel with this callback
    /// handler thread should call `run()` on this object
    auto create_receiver(callback_t &&cb) {
        std::scoped_lock r_l(r_lock_);
        auto rv =
            receivers.emplace_back(std::make_shared<receiver>(std::move(cb)));
        rv->stop_ = state::running;
        return rv;
    }
    class receiver {
    public:
        receiver(callback_t &&cb) : callback_(std::move(cb)) {
        }
        /// wait for new objects on the channel and call the callback for them
        void run();

    private:
        void stop(state st) {
            stop_ = st;
            cv_.notify_all();
        }
        callback_t callback_;
        std::deque<std::shared_ptr<T>> q_;
        std::mutex q_lock_;
        std::condition_variable cv_;
        std::atomic<state> stop_{ state::closed };
        friend class mcast_chan<T>;
    };

    void remove_receiver(const std::shared_ptr<receiver> &r) {
        std::scoped_lock r_l(r_lock_);
        auto i = receivers.remove_if([&](auto &&e) { return e == r; });
        if (i != receivers.end()) {
            (*i)->stop(state::closed);
            receivers.erase(i);
        }
    }

private:
    std::list<std::shared_ptr<receiver>> receivers;
    mutable std::mutex r_lock_;
};

template <typename T> void mcast_chan<T>::send_ptr(const obj_ptr_t &obj) {
    std::scoped_lock r_l(r_lock_);
    for (const auto &rcv : receivers) {
        std::scoped_lock q_l{ rcv->q_lock_ };
        rcv->q_.emplace_back(obj);
        rcv->cv_.notify_one();
    }
}

template <typename T>
template <typename... U>
void mcast_chan<T>::send(U &&...args) {
    auto o = std::make_shared<T>(std::forward<U>(args)...);
    send_ptr(o);
}

template <typename T> void mcast_chan<T>::receiver::run() {
    std::unique_lock q_l(q_lock_);
    while (stop_ == state::running) {
        cv_.wait(q_l,
                 [&] { return stop_.load() != state::running || !q_.empty(); });
        if (stop_ == state::stopped)
            break;
        while (!q_.empty()) {
            auto i = std::move(q_.front());
            q_.pop_front();
            q_l.unlock();
            callback_(*i);
            i.reset();
            q_l.lock();
        }
    }
}

template <typename T> void mcast_chan<T>::close(bool abandon_q) {
    auto stop_ = abandon_q ? state::stopped : state::closed;
    std::unique_lock r_l(r_lock_);
    for (auto &rcv : receivers) {
        rcv->stop(stop_);
    }
}
