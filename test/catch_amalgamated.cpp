// Auto-registration test runner for the local Catch-compatible shim.
#include "catch_amalgamated.hpp"

std::vector<TestCase>& getRegisteredTests() {
    static std::vector<TestCase> tests;
    return tests;
}

ActiveTestContext*& getActiveTestContext() {
    static thread_local ActiveTestContext* ctx = nullptr;
    return ctx;
}

void recordCheckFailure(const std::string& message) {
    auto* ctx = getActiveTestContext();
    if (ctx == nullptr) {
        throw TestAssertionFailed(message);
    }
    ctx->checkFailures++;
    ctx->infoMessages.push_back(message);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const auto& tests = getRegisteredTests();
    if (tests.empty()) {
        std::cout << "[Test Runner] No tests registered!" << std::endl;
        return 1;
    }

    int passed = 0;
    int failed = 0;
    std::cout << "[Test Runner] Running " << tests.size() << " test(s)..." << std::endl;

    for (const auto& tc : tests) {
        std::cout << "  RUN: " << tc.name << " " << tc.tags << std::endl;
        ActiveTestContext ctx;
        getActiveTestContext() = &ctx;

        try {
            tc.fn();
            if (ctx.checkFailures > 0) {
                for (const auto& msg : ctx.infoMessages) {
                    std::cerr << "    " << msg << std::endl;
                }
                std::cerr << "  FAIL: " << tc.name << " - " << ctx.checkFailures
                          << " CHECK assertion(s) failed" << std::endl;
                failed++;
            } else {
                std::cout << "  PASS: " << tc.name << std::endl;
                passed++;
            }
        } catch (const std::exception& e) {
            for (const auto& msg : ctx.infoMessages) {
                std::cerr << "    " << msg << std::endl;
            }
            std::cerr << "  FAIL: " << tc.name << " - " << e.what() << std::endl;
            failed++;
        } catch (...) {
            for (const auto& msg : ctx.infoMessages) {
                std::cerr << "    " << msg << std::endl;
            }
            std::cerr << "  FAIL: " << tc.name << " - unknown exception" << std::endl;
            failed++;
        }

        getActiveTestContext() = nullptr;
    }

    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed
              << " failed out of " << tests.size() << std::endl;

    if (failed > 0) {
        std::cout << "Overall: FAILED" << std::endl;
        return 1;
    }

    std::cout << "Overall: ALL TESTS PASSED" << std::endl;
    return 0;
}
