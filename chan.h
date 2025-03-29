#pragma once
#include <memory>
#include <atomic>
#include <functional>
#include <deque>
#include <list>
#include <condition_variable>
#include "util.h"

template <typename T> class mcast_chan {
public:
    using cb_t = std::function<void(const T &)>;
    using ptr_t = std::shared_ptr<T>;
    // receiver thread entry
    void receiver(cb_t callback);
    void send_ptr(const ptr_t&);
    // send a newly constructed T(args) to receivers
    template <typename... U> void send(U &&...args);
    void close(bool abandon_q = false);

private:
    enum class state { running, closed, stopped };
    struct receiver_ {
        receiver_(cb_t &&cb)
            : callback_(std::move(cb)) {
        }
        cb_t callback_;
        std::deque<std::shared_ptr<T>> q_;
        std::mutex q_lock_;
        std::condition_variable cv_;
    };
    std::list<receiver_> receivers;
    std::mutex r_lock_;
    std::atomic<state> stop_{ state::running };
};

template <typename T> void mcast_chan<T>::send_ptr(const ptr_t& obj) {
    std::scoped_lock r_l(r_lock_);
    for (auto &rcv : receivers) {
        std::scoped_lock q_l{ rcv.q_lock_ };
        rcv.q_.emplace_back(obj);
        rcv.cv_.notify_one();
    }
}

template <typename T> template <typename... U> void mcast_chan<T>::send(U&&...args) {
    auto o = std::make_shared<T>(std::forward<U>(args)...);
    send_ptr(o);
}

template <typename T> void mcast_chan<T>::receiver(cb_t callback) {
    std::unique_lock r_l(r_lock_);
    auto &this_one = receivers.emplace_back(std::move(callback));
    auto this_one_it = --receivers.end();
    r_l.unlock();
    utils::defer e([&]() {
        r_l.lock();
        receivers.erase(this_one_it);
    });

    std::unique_lock q_l(this_one.q_lock_);
    while (stop_ == state::running) {
        this_one.cv_.wait(q_l, [&] {
            return stop_.load() != state::running || !this_one.q_.empty();
        });
        if (stop_ == state::stopped)
            break;
        while (!this_one.q_.empty()) {
            auto i = std::move(this_one.q_.front());
            this_one.q_.pop_front();
            q_l.unlock();
            this_one.callback_(*i);
            i.reset();
            q_l.lock();
        }
    }
}

template <typename T> void mcast_chan<T>::close(bool abandon_q) {
    stop_ = abandon_q ? state::stopped : state::closed;
    std::unique_lock r_l(r_lock_);
    for (auto &rcv : receivers) {
        rcv.cv_.notify_all();
    }
}
