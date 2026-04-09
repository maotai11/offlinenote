// test/catch_amalgamated.hpp — Vendored Catch2 stub with AUTO-REGISTRATION
// Replace with real Catch2 from https://github.com/catchorg/Catch2
#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstdlib>

struct TestCase {
    std::string name;
    std::string tags;
    std::function<void()> fn;
};

// Single global instance (defined in catch_amalgamated.cpp)
std::vector<TestCase>& getRegisteredTests();

// Registrar struct — constructor adds to registry
struct TestCaseRegistrar {
    TestCaseRegistrar(const char* name, const char* tags, std::function<void()> fn) {
        getRegisteredTests().push_back({name, tags, std::move(fn)});
    }
};

#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)
#define TEST_CASE(name, ...) \
    static void CONCAT1(test_body_, __LINE__)(); \
    struct CONCAT1(test_reg_, __LINE__) { \
        CONCAT1(test_reg_, __LINE__)() { \
            getRegisteredTests().push_back({name, #__VA_ARGS__, CONCAT1(test_body_, __LINE__)}); \
        } \
    }; \
    static CONCAT1(test_reg_, __LINE__) CONCAT1(test_reg_instance_, __LINE__); \
    static void CONCAT1(test_body_, __LINE__)()

#define SECTION(name) if(true)
#define REQUIRE(x) do { if(!(x)) { std::cerr << "REQUIRE failed: " << #x << " at line " << __LINE__ << std::endl; exit(1); } } while(0)
#define REQUIRE_FALSE(x) REQUIRE(!(x))
#define INFO(x)
#define SUCCEED(msg) std::cout << "  SUCCEED: " << msg << std::endl
