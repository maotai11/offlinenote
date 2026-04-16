// Minimal Catch2-compatible test shim with auto-registration.
// This intentionally implements only the subset used by this repository.
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase {
    std::string name;
    std::string tags;
    std::function<void()> fn;
};

struct ActiveTestContext {
    int checkFailures = 0;
    std::vector<std::string> infoMessages;
};

std::vector<TestCase>& getRegisteredTests();
ActiveTestContext*& getActiveTestContext();
void recordCheckFailure(const std::string& message);

struct TestCaseRegistrar {
    TestCaseRegistrar(const char* name, const char* tags, std::function<void()> fn) {
        getRegisteredTests().push_back({name, tags, std::move(fn)});
    }
};

#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)

#define TEST_CASE(name, ...) \
    static void CONCAT1(test_body_, __LINE__)(); \
    static TestCaseRegistrar CONCAT1(test_registrar_, __LINE__)(name, #__VA_ARGS__, CONCAT1(test_body_, __LINE__)); \
    static void CONCAT1(test_body_, __LINE__)()

#define SECTION(name) if (true)

class TestAssertionFailed : public std::runtime_error {
public:
    explicit TestAssertionFailed(const std::string& msg) : std::runtime_error(msg) {}
};

#define REQUIRE(x) do { \
    if (!(x)) { \
        throw TestAssertionFailed(std::string("REQUIRE failed: ") + #x + " at line " + std::to_string(__LINE__)); \
    } \
} while (0)

#define REQUIRE_FALSE(x) REQUIRE(!(x))

#define CHECK(x) do { \
    if (!(x)) { \
        recordCheckFailure(std::string("CHECK failed: ") + #x + " at line " + std::to_string(__LINE__)); \
    } \
} while (0)

#define CHECK_FALSE(x) CHECK(!(x))

#define INFO(x) do { \
    std::ostringstream CONCAT1(test_info_stream_, __LINE__); \
    CONCAT1(test_info_stream_, __LINE__) << x; \
    if (getActiveTestContext() != nullptr) { \
        getActiveTestContext()->infoMessages.push_back(CONCAT1(test_info_stream_, __LINE__).str()); \
    } \
} while (0)

#define SUCCEED(msg) std::cout << "  SUCCEED: " << msg << std::endl
