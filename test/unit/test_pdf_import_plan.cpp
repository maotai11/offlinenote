#include "../catch_amalgamated.hpp"
#include "../../src/util/PdfImportPlan.h"

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("PdfImportPlan fits content without distortion", "[pdf][layout]") {
    const auto fit = PdfImportPlan::fitContain(800.0, 600.0, 400.0, 200.0);

    REQUIRE(fit.valid);
    REQUIRE(std::fabs(fit.scale - 2.0) < 1e-9);
    REQUIRE(std::fabs(fit.width - 800.0) < 1e-9);
    REQUIRE(std::fabs(fit.height - 400.0) < 1e-9);
    REQUIRE(std::fabs(fit.offsetX - 0.0) < 1e-9);
    REQUIRE(std::fabs(fit.offsetY - 100.0) < 1e-9);
}

TEST_CASE("PdfImportPlan rejects invalid fit dimensions", "[pdf][layout]") {
    const auto fit = PdfImportPlan::fitContain(0.0, 600.0, 400.0, 200.0);

    REQUIRE_FALSE(fit.valid);
}

TEST_CASE("PdfImportPlan chooses unique PDF import directories", "[pdf][import]") {
    const fs::path root = fs::current_path() / "pdf_import_plan_root";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    fs::create_directories(root / "project_pdf");

    const fs::path uniqueDir = PdfImportPlan::uniquePdfImportDirectory(root, "project");
    REQUIRE(uniqueDir == root / "project_pdf_1");
}

TEST_CASE("PdfImportPlan sanitizes unsafe directory stems", "[pdf][import][security]") {
    const fs::path root = fs::current_path() / "pdf_import_plan_sanitize";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    const fs::path uniqueDir = PdfImportPlan::uniquePdfImportDirectory(root, R"(bad:name\..\demo)");
    REQUIRE(uniqueDir.filename().generic_u8string() == "badname_demo_pdf");
}
