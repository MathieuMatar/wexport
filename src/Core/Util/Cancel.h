#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>

namespace ck {

// Shared cancel flag. Copies refer to the same flag.
class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void Cancel() const { flag_->store(true); }
    bool IsCancelled() const { return flag_->load(); }
    void ThrowIfCancelled() const;

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

struct OperationCancelled : std::runtime_error {
    OperationCancelled() : std::runtime_error("Cancelled") {}
};

inline void CancelToken::ThrowIfCancelled() const {
    if (IsCancelled()) throw OperationCancelled();
}

}  // namespace ck
