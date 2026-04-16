param(
    [Parameter(Mandatory = $true)]
    [string]$CMakeFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (!(Test-Path $CMakeFile)) {
    throw "CMake file not found: $CMakeFile"
}

$content = Get-Content -Raw $CMakeFile

$excludedStubSources = @(
    "src/document/ImageElement.cpp",
    "src/export/ExportManager.cpp",
    "src/export/PdfExporter.cpp",
    "src/export/PngExporter.cpp",
    "src/import/ImageImporter.cpp",
    "src/input/GestureHandler.cpp",
    "src/input/InputRouter.cpp",
    "src/input/Stabilizer.cpp",
    "src/input/StrokeBuilder.cpp",
    "src/input/StylusHandler.cpp",
    "src/input/ToolState.cpp",
    "src/platform/CrashLogger.cpp",
    "src/platform/FontManager.cpp",
    "src/platform/PlatformWindows.cpp",
    "src/platform/ThemeManager.cpp",
    "src/rendering/BackgroundRenderer.cpp",
    "src/rendering/ImageRenderer.cpp",
    "src/rendering/PdfPageRenderer.cpp",
    "src/rendering/RenderCache.cpp",
    "src/rendering/Renderer.cpp",
    "src/rendering/StrokeRenderer.cpp",
    "src/tools/EraserTool.cpp",
    "src/tools/PenTool.cpp",
    "src/tools/SelectionTool.cpp",
    "src/tools/ToolManager.cpp",
    "src/ui/MainToolbar.cpp",
    "src/ui/MenuBar.cpp",
    "src/ui/PageCanvas.cpp",
    "src/ui/StatusBar.cpp",
    "src/ui/dialogs/ExportDialog.cpp",
    "src/ui/dialogs/NewDocumentDialog.cpp",
    "src/ui/widgets/ColorButton.cpp",
    "src/ui/widgets/PenSizeSlider.cpp",
    "src/ui/widgets/ZoomController.cpp"
)

$found = @()
foreach ($source in $excludedStubSources) {
    if ($content -match [regex]::Escape($source)) {
        $found += $source
    }
}

if ($found.Count -gt 0) {
    Write-Host "Fake-success stub audit failed. These excluded sources are back in the build:" -ForegroundColor Red
    $found | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Fake-success stub audit passed." -ForegroundColor Green
