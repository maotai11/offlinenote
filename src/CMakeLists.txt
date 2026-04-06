# src/CMakeLists.txt
# SPDX-License-Identifier: GPL-2.0-or-later

# ── 收集所有原始碼檔案
target_sources(offlinenote PRIVATE
    main.cpp

    # Application layer
    application/Application.cpp
    application/AppController.cpp
    application/Config.cpp
    application/PathManager.cpp
    application/StartupCheck.cpp

    # Document model
    document/Document.cpp
    document/Page.cpp
    document/Layer.cpp
    document/Stroke.cpp
    document/ImageElement.cpp
    document/PageBackground.cpp
    document/PdfBackground.cpp
    document/DocumentMetadata.cpp

    # Serialization
    serialization/NoteSerializer.cpp
    serialization/NoteDeserializer.cpp
    serialization/SafeDecompressor.cpp
    serialization/SecureXmlParser.cpp
    serialization/XmlHelper.cpp
    serialization/MigrationManager.cpp

    # Rendering
    rendering/Renderer.cpp
    rendering/StrokeRenderer.cpp
    rendering/BackgroundRenderer.cpp
    rendering/PdfPageRenderer.cpp
    rendering/ImageRenderer.cpp
    rendering/RenderCache.cpp

    # Input
    input/InputRouter.cpp
    input/StylusHandler.cpp
    input/GestureHandler.cpp
    input/StrokeBuilder.cpp
    input/Stabilizer.cpp
    input/ToolState.cpp

    # Tools
    tools/ToolManager.cpp
    tools/PenTool.cpp
    tools/EraserTool.cpp
    tools/SelectionTool.cpp

    # Export
    export/ExportManager.cpp
    export/PdfExporter.cpp
    export/PngExporter.cpp

    # Import
    import/PdfImporter.cpp
    import/ImageImporter.cpp

    # UI
    ui/MainWindow.cpp
    ui/MainToolbar.cpp
    ui/PageCanvas.cpp
    ui/MenuBar.cpp
    ui/StatusBar.cpp
    ui/dialogs/NewDocumentDialog.cpp
    ui/dialogs/ExportDialog.cpp
    ui/dialogs/AboutDialog.cpp
    ui/widgets/ColorButton.cpp
    ui/widgets/PenSizeSlider.cpp
    ui/widgets/ZoomController.cpp

    # Platform
    platform/FontManager.cpp
    platform/ThemeManager.cpp
    platform/CrashLogger.cpp
    platform/FileLock.cpp
    platform/AtomicRename.cpp

    # Util
    util/Logger.cpp
    util/StringUtils.cpp
    util/FileUtils.cpp
    util/PathValidator.cpp
)

# 平台特定實作
if(UNIX AND NOT APPLE)
    target_sources(offlinenote PRIVATE platform/PlatformLinux.cpp)
elseif(APPLE)
    target_sources(offlinenote PRIVATE platform/PlatformMacOS.cpp)
elseif(WIN32)
    target_sources(offlinenote PRIVATE platform/PlatformWindows.cpp)
endif()

# ── 核心函式庫也包含相同來源（供測試使用）
target_sources(offlinenote_core_lib PRIVATE
    application/Config.cpp
    application/PathManager.cpp
    application/StartupCheck.cpp

    document/Document.cpp
    document/Page.cpp
    document/Layer.cpp
    document/Stroke.cpp
    document/ImageElement.cpp
    document/PageBackground.cpp
    document/PdfBackground.cpp
    document/DocumentMetadata.cpp

    serialization/NoteSerializer.cpp
    serialization/NoteDeserializer.cpp
    serialization/SafeDecompressor.cpp
    serialization/SecureXmlParser.cpp
    serialization/XmlHelper.cpp
    serialization/MigrationManager.cpp

    rendering/Renderer.cpp
    rendering/StrokeRenderer.cpp
    rendering/BackgroundRenderer.cpp
    rendering/PdfPageRenderer.cpp
    rendering/ImageRenderer.cpp
    rendering/RenderCache.cpp

    input/InputRouter.cpp
    input/StylusHandler.cpp
    input/GestureHandler.cpp
    input/StrokeBuilder.cpp
    input/Stabilizer.cpp
    input/ToolState.cpp

    tools/ToolManager.cpp
    tools/PenTool.cpp
    tools/EraserTool.cpp
    tools/SelectionTool.cpp

    export/ExportManager.cpp
    export/PdfExporter.cpp
    export/PngExporter.cpp

    import/PdfImporter.cpp
    import/ImageImporter.cpp

    platform/FontManager.cpp
    platform/ThemeManager.cpp
    platform/CrashLogger.cpp
    platform/FileLock.cpp
    platform/AtomicRename.cpp

    util/Logger.cpp
    util/StringUtils.cpp
    util/FileUtils.cpp
    util/PathValidator.cpp
)

if(UNIX AND NOT APPLE)
    target_sources(offlinenote_core_lib PRIVATE platform/PlatformLinux.cpp)
elseif(APPLE)
    target_sources(offlinenote_core_lib PRIVATE platform/PlatformMacOS.cpp)
elseif(WIN32)
    target_sources(offlinenote_core_lib PRIVATE platform/PlatformWindows.cpp)
endif()
