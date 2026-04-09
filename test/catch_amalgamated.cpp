// test/catch_amalgamated.cpp — Auto-registration test runner
#include "catch_amalgamated.hpp"

// Define the global test registry
std::vector<TestCase>& getRegisteredTests() {
    static std::vector<TestCase> tests;
    return tests;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    const auto& tests = getRegisteredTests();

    if (tests.empty()) {
        std::cout << "[Test Runner] No tests registered!" << std::endl;
        std::cout << "TODO: Replace catch_amalgamated stub with real Catch2 v3." << std::endl;
        return 0;
    }

    int passed = 0, failed = 0;
    std::cout << "[Test Runner] Running " << tests.size() << " test(s)..." << std::endl;

    for (const auto& tc : tests) {
        std::cout << "  RUN: " << tc.name << " " << tc.tags << std::endl;
        try {
            tc.fn();
            std::cout << "  PASS: " << tc.name << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  FAIL: " << tc.name << " - " << e.what() << std::endl;
            failed++;
        } catch (...) {
            std::cerr << "  FAIL: " << tc.name << " - unknown exception" << std::endl;
            failed++;
        }
    }

    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed out of " << tests.size() << std::endl;

    if (failed > 0) {
        std::cout << "Overall: FAILED" << std::endl;
        return 1;
    }
    std::cout << "Overall: ALL TESTS PASSED" << std::endl;
    return 0;
}
