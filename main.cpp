#include <iostream>
#include <string>
#include <thread>
#include <tuple>
#include "fswalker.h"
#include "chan.h"
#include "util.h"
namespace {
struct scan_context {
    // filename, full path
    using input_type =
        std::tuple<std::string, std::filesystem::path>;
    // full path, matched string
    using result_type =
        std::tuple<std::string, std::string>;
    void signal_stop();
    void signal_complete();
    void signal_dump() {
        dump_cv.notify_all();
    }
    void dump();
    template <typename... Rs> void add(Rs &&...res) {
        std::scoped_lock l(result_lock);
        result.emplace_back(std::forward<Rs>(res)...);
    }
    void ui_worker();
    void dump_worker();
    mcast_chan<input_type> chan;
    utils::pipe ui_thr_control;
    bool is_running() const {
        return stopf == stop_type::running;
    }

private:
    enum class stop_type { running, finished, stopped };
    std::atomic<stop_type> stopf{ stop_type::running };
    std::vector<result_type> result;
    std::mutex result_lock;
    std::condition_variable dump_cv;
};

void scan_context::signal_stop() {
    stopf = stop_type::stopped;
    chan.close(true);
    dump_cv.notify_all();
    (void)::write(ui_thr_control.write_fd(), "1", 1);
}

void scan_context::signal_complete() {
    auto exp = stop_type::running;
    if (stopf.compare_exchange_strong(exp, stop_type::finished)) {
        stopf = stop_type::finished;
        chan.close();
        dump_cv.notify_all();
        (void)::write(ui_thr_control.write_fd(), "1", 1);
    }
}
void print_w_highlight(const std::string& str, const std::string& sub) {
    auto base = str.find_last_of(std::filesystem::path::preferred_separator);
    base = base == std::string::npos ? 0 : base + 1;
    std::cout << str.substr(0, base);
    auto pos = base;
    for(auto pos = base; pos < str.size();) {
        auto m = str.find(sub, pos);
        if (m == std::string::npos) {
            std::cout << str.substr(pos);
            break;
        }
        std::cout << str.substr(pos, m - pos);
        std::cout << "\033[1m" << sub << "\033[0m";
        pos = m + sub.size();
    }
    std::cout << "\n";
}

void scan_context::dump() {
    std::unique_lock l(result_lock);
    auto r = std::move(result);
    l.unlock();
    if (stopf == stop_type::stopped)
        return;
    for (const auto &s : r) {
        auto &path = std::get<0>(s);
        auto &sub = std::get<1>(s);
        if(0 && isatty(STDOUT_FILENO) && !sub.empty()) {
            print_w_highlight(path, sub);
        } else
            std::cout << path << "\n";
    }
}

void scan_context::ui_worker() {
    utils::interruptible_buf buf(STDIN_FILENO, ui_thr_control.read_fd());
    std::istream input(&buf);
    while (stopf == stop_type::running) {
        std::string cmd;
        if(!std::getline(input, cmd))
            break;
        if (cmd == "dump") {
            signal_dump();
        } else if (cmd == "exit") {
            signal_stop();
        }
    }
}

void scan_context::dump_worker() {
    while (stopf == scan_context::stop_type::running) {
        {
            std::unique_lock l(result_lock);
            dump_cv.wait_for(l, std::chrono::seconds(5));
        }
        dump();
    }
}

struct walker : public fswalker {
    scan_context &ctx;
    explicit walker(scan_context &c)
        : ctx(c) {
    }
    void process(const std::filesystem::path &p) override {
        ctx.chan.send(std::make_tuple(p.filename(), p));
    }
    bool is_stopped() override {
        return !ctx.is_running();
    }
    void error(const std::exception &e,
               const std::filesystem::path &path) override {
        std::cerr << path << ": " << e.what() << "\n";
    }
};

void scan(const std::string &root, std::vector<std::string> &subs) {
    scan_context ctx;
    walker w(ctx);

    std::vector<std::thread> matchers;
    for (auto &sub : subs) {
        matchers.emplace_back([&, s = std::move(sub)] {
            ctx.chan.receiver([&](auto &&obj) {
                auto &fn = std::get<0>(obj);
                if (fn.find(s) != std::string::npos)
                    ctx.add(std::get<1>(obj), s);
            });
        });
    }
    auto ui_thr = std::thread([&]() { ctx.ui_worker(); });
    auto dump_thr = std::thread([&]() { ctx.dump_worker(); });

    w.scan(root);
    ctx.signal_complete();

    for (auto &thr : matchers) {
        thr.join();
    }
    ui_thr.join();
    dump_thr.join();
    ctx.dump();
}

void usage(int argc, const char *argv[]) {
    std::cout
        << "Usage: " << ((argc > 0 && argv[0]) ? argv[0] : "file-finder")
        << " <dir> <substring1>[<substring2> [<substring3>]...]\n";
}
} // end anon namespace

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        usage(argc, argv);
        return 1;
    }
    std::vector<std::string> subs(argv + 2, argv + argc);
    scan(argv[1], subs);
    return 0;
}