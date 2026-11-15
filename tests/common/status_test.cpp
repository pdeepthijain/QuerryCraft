#include <gtest/gtest.h>
#include "common/status.hpp"

using namespace querycraft::common;

TEST(StatusTest, OkStatusDefault) {
    Status s;
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::Ok);
    EXPECT_EQ(s.to_string(), "OK");
}

TEST(StatusTest, OkStatusExplicit) {
    Status s = Status::Ok();
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::Ok);
    EXPECT_TRUE(s.message().empty());
}

TEST(StatusTest, NotFoundStatus) {
    Status s = Status::NotFound("table 'users' does not exist");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::NotFound);
    EXPECT_EQ(s.message(), "table 'users' does not exist");
    EXPECT_EQ(s.to_string(), "Not Found: table 'users' does not exist");
}

TEST(StatusTest, AlreadyExistsStatus) {
    Status s = Status::AlreadyExists("table 'users' already exists");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::AlreadyExists);
    EXPECT_EQ(s.to_string(), "Already Exists: table 'users' already exists");
}

TEST(StatusTest, InvalidArgumentStatus) {
    Status s = Status::InvalidArgument("page_id cannot be negative");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::InvalidArgument);
    EXPECT_EQ(s.to_string(), "Invalid Argument: page_id cannot be negative");
}

TEST(StatusTest, NotSupportedStatus) {
    Status s = Status::NotSupported("transactions not yet implemented");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::NotSupported);
    EXPECT_EQ(s.to_string(), "Not Supported: transactions not yet implemented");
}

TEST(StatusTest, IoErrorStatus) {
    Status s = Status::IoError("failed to write page to disk");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::IoError);
    EXPECT_EQ(s.to_string(), "I/O Error: failed to write page to disk");
}

TEST(StatusTest, InternalErrorStatus) {
    Status s = Status::InternalError("buffer frame corruption detected");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::InternalError);
    EXPECT_EQ(s.to_string(), "Internal Error: buffer frame corruption detected");
}

TEST(StatusTest, SyntaxErrorStatus) {
    Status s = Status::SyntaxError("unexpected character '@' at line 1, col 5");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::SyntaxError);
    EXPECT_EQ(s.message(), "unexpected character '@' at line 1, col 5");
    EXPECT_EQ(s.to_string(), "Syntax Error: unexpected character '@' at line 1, col 5");
}
