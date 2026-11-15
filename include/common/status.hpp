#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace querycraft::common {

enum class StatusCode : std::uint8_t {
    Ok = 0,
    NotFound,
    AlreadyExists,
    InvalidArgument,
    NotSupported,
    IoError,
    InternalError
};

class [[nodiscard]] Status {
public:
    Status() noexcept : code_(StatusCode::Ok), message_("") {}

    explicit Status(StatusCode code, std::string message = "") noexcept
        : code_(code), message_(std::move(message)) {}

    static Status Ok() noexcept {
        return Status(StatusCode::Ok);
    }

    static Status NotFound(std::string message) {
        return Status(StatusCode::NotFound, std::move(message));
    }

    static Status AlreadyExists(std::string message) {
        return Status(StatusCode::AlreadyExists, std::move(message));
    }

    static Status InvalidArgument(std::string message) {
        return Status(StatusCode::InvalidArgument, std::move(message));
    }

    static Status NotSupported(std::string message) {
        return Status(StatusCode::NotSupported, std::move(message));
    }

    static Status IoError(std::string message) {
        return Status(StatusCode::IoError, std::move(message));
    }

    static Status InternalError(std::string message) {
        return Status(StatusCode::InternalError, std::move(message));
    }

    [[nodiscard]] bool ok() const noexcept {
        return code_ == StatusCode::Ok;
    }

    [[nodiscard]] StatusCode code() const noexcept {
        return code_;
    }

    [[nodiscard]] const std::string& message() const noexcept {
        return message_;
    }

    [[nodiscard]] std::string to_string() const {
        if (ok()) {
            return "OK";
        }
        std::string result = code_to_string(code_);
        if (!message_.empty()) {
            result += ": ";
            result += message_;
        }
        return result;
    }

private:
    static std::string code_to_string(StatusCode code) {
        switch (code) {
            case StatusCode::Ok:
                return "OK";
            case StatusCode::NotFound:
                return "Not Found";
            case StatusCode::AlreadyExists:
                return "Already Exists";
            case StatusCode::InvalidArgument:
                return "Invalid Argument";
            case StatusCode::NotSupported:
                return "Not Supported";
            case StatusCode::IoError:
                return "I/O Error";
            case StatusCode::InternalError:
                return "Internal Error";
            default:
                return "Unknown Error";
        }
    }

    StatusCode code_;
    std::string message_;
};

} // namespace querycraft::common
