#pragma once
#include <filesystem>
#include <functional>
#include <string>

/** stateless base for filesystem walker
 * implementations need to provide \c process() \c error() and \c is_stopped() callbacks
 */
class fswalker {
public:
    virtual ~fswalker() = default;
    /// this function is called by scan() for each filesystem object
    virtual void process(const std::filesystem::path &) = 0;
    /// called by scan() on error
    virtual void error(const std::exception &, const std::filesystem::path &) {
    }
    /// called by scan() after every object, scan it terminated if this returns true
    virtual bool is_stopped() {
        return false;
    }
    /// walk the directory tree from root_path calling `process()` for each path
    void scan(const std::filesystem::path &root_path);

protected:
    // optional resource limits (approximate)
    size_t memory_limit = 0;
    size_t depth_limit = 1024;
};