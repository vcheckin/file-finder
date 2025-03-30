#pragma once
#include <iostream>
#include <streambuf>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <thread>

namespace utils {
/// utility to make istream "interruptible"
/// anything on exit_fd unblocks the stream
class interruptible_buf : public std::streambuf {
public:
    interruptible_buf(int read_fd, int exit_fd) :
        read_fd_(read_fd), signal_fd_(exit_fd) {
        setg(buffer_, buffer_, buffer_);
    }

protected:
    int underflow() override {
        struct pollfd fds[2] = { { .fd = read_fd_, .events = POLLIN },
                                 { .fd = signal_fd_, .events = POLLIN } };

        int ret = ::poll(fds, 2, -1);
        if (ret <= 0) {
            return traits_type::eof();
        }

        if (fds[1].revents & POLLIN) {
            return traits_type::eof();
        }

        if (fds[0].revents & POLLIN) {
            ssize_t n = ::read(read_fd_, buffer_, buf_size);
            if (n <= 0) {
                return traits_type::eof();
            }
            setg(buffer_, buffer_, buffer_ + n);
            return traits_type::to_int_type(*gptr());
        }
        return traits_type::eof();
    }

private:
    static constexpr size_t buf_size = 128;
    int read_fd_, signal_fd_;
    traits_type::char_type buffer_[buf_size];
};

class pipe {
public:
    pipe() {
        if (::pipe(pipe_fd) == -1) {
            throw std::system_error(errno, std::system_category(),
                                    "pipe failed");
        }
    }
    pipe(const pipe &) = delete;
    int read_fd() const {
        return pipe_fd[0];
    }
    int write_fd() const {
        return pipe_fd[1];
    }
    ~pipe() {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
    }

private:
    int pipe_fd[2];
};

}
template <typename... Ts>
std::ostream &operator<<(std::ostream &os, const std::tuple<Ts...> &tuple) {
    std::apply(
        [&os](Ts const &...tupleArgs) {
            os << '[';
            std::size_t n{ 0 };
            ((os << tupleArgs << (++n != sizeof...(Ts) ? ", " : "")), ...);
            os << ']';
        },
        tuple);
    return os;
}
