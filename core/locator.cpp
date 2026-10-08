#include "locator.h"

#include "log.h"

namespace mtags {

namespace {

// The original logged the offending value and then threw exception_io_data(),
// whose default message is "Unsupported format or corrupted file". Both are
// reproduced here so a console log from this build reads like the original's.
unsigned reject_index(const std::string& shown) {
    log_error("Invalid subsong index: %s", shown.c_str());
    throw exception_io_data();
}

} // namespace

t_locator t_locator::parse(const std::string& raw) {
    if (raw.size() < 2) {
        log_error("Invalid path: %s", raw.c_str());
        throw exception_io_data();
    }

    t_locator out;
    const size_t bar = raw.find_last_of('|');
    if (bar == std::string::npos) {
        // No trace here: the original logged "Resolving locator" from the
        // resolver, not from this split.
        out.path = raw;
        out.index = 0;
        return out;
    }

    out.path = raw.substr(0, bar);

    // The suffix is a decimal subsong. A non-numeric suffix is not an index,
    // and is rejected the same way an out-of-range one is.
    const std::string digits = raw.substr(bar + 1);
    unsigned value = 0;
    for (char c : digits) {
        if (c < '0' || c > '9') {
            reject_index(digits);
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
    }

    out.index = value;
    return out;
}

unsigned t_locator::index_within(size_t set_count) const {
    if (index == 0) {
        return 1;
    }
    if (index > set_count) {
        reject_index(std::to_string(index));
    }
    return index;
}

} // namespace mtags
