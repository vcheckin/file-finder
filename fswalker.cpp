#include "fswalker.h"

#include <iostream>
#include <thread>
#include <stack>

#include <iostream>

namespace fs = std::filesystem;

/*
 * shielding from some potential adversary layouts here:
 * . deep hierachies (can't be recursive or depth first with open iterators)
 * . lots of files/dirs with long path leading to them (can't store full paths from directory iterator)
 */

namespace {
struct scan_ctx {
    size_t footprint = 0;
    size_t footprint_hwm = 0;
    void add(size_t s) {
        footprint += s;
        footprint_hwm = std::max(footprint, footprint_hwm);
    }
    void sub(size_t s) {
        footprint -= s;
    }
};

struct fsobject {
    using ptr = std::shared_ptr<fsobject>;
    fsobject(scan_ctx &c, std::string fn, ptr p) :
        ctx(c), filename(std::move(fn)), parent(p) {
        ctx.add(sizeof(*this) + filename.size());
        if (p)
            level = p->level + 1;
    }
    ~fsobject() {
        ctx.sub(sizeof(*this) + filename.size() + path_len);
    }
    const fs::path &path() {
        if (!path_.empty())
            return path_;
        path_ = parent ? parent->path() / filename : fs::path(filename);
        path_len = filename.size() + (parent ? parent->path_len : 0);
        ctx.add(path_len);
        return path_;
    }
    scan_ctx &ctx;
    std::string filename;
    std::shared_ptr<fsobject> parent;
    fs::path path_; // only stored temporarily while inside this directory
    unsigned level{ 0 };
    unsigned path_len{ 0 };
};
}
void fswalker::scan(const std::filesystem::path &root_path) {
    if (!fs::is_directory(root_path)) {
        if (fs::exists(root_path))
            process(root_path);
        else
            error(std::system_error(std::make_error_code(std::errc::no_such_file_or_directory)), root_path);
        return;
    }
    scan_ctx ctx;
    auto obj = std::make_shared<fsobject>(ctx, root_path.string(), nullptr);
    std::stack<fsobject::ptr> q;
    q.push(obj);
    while (!q.empty() && !is_stopped()) {
        auto o = std::move(q.top());
        q.pop();
        auto add = true;
        try {
            for (const auto &dirent : fs::directory_iterator(o->path())) {
                if (is_stopped())
                    break;
                auto curr_path = dirent.path();
                process(curr_path);
                if (add && memory_limit && ctx.footprint > memory_limit) {
                    error(std::system_error(std::make_error_code(
                              std::errc::not_enough_memory)),
                          o->path());
                    add = false;
                }
                if (add && depth_limit && o->level > depth_limit) {
                    error(std::system_error(std::make_error_code(
                              std::errc::filename_too_long)),
                          o->path());
                    add = false;
                }
                if (add && fs::is_directory(dirent.symlink_status())) {
                    q.push(std::make_shared<fsobject>(
                        ctx, curr_path.filename().string(), o));
                }
            }
        } catch (const std::exception &e) {
            error(e, o->path());
        }
    }
}
