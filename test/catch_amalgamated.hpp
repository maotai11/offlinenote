// test/catch_amalgamated.hpp — Vendored Catch2 stub
// Replace with real Catch2 from https://github.com/catchorg/Catch2
#pragma once
#define CATCH_CONFIG_MAIN
#include <iostream>
#include <string>
#include <cmath>
#include <sstream>
#include <iomanip>

// Simple stub: each TEST_CASE becomes a unique function via __COUNTER__
#define CONCAT2(a, b) a##b
#define CONCAT1(a, b) CONCAT2(a, b)
#define TEST_CASE(name, ...) void CONCAT1(test_fn_, __COUNTER__)(); void CONCAT1(test_fn_, __COUNTER__)()
#define SECTION(name) if(true)
#define REQUIRE(x) do { if(!(x)) { std::cerr << "REQUIRE failed: " << #x << " at line " << __LINE__ << std::endl; exit(1); } } while(0)
#define REQUIRE_FALSE(x) REQUIRE(!(x))
#define INFO(x)
#define SUCCEED(msg) std::cout << msg << std::endl
