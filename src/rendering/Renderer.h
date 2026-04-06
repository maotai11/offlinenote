// src/rendering/Renderer.h
#pragma once
#include <cairo.h>
class Page;
struct RenderContext { double scale = 1.0; };

class Renderer {
public:
    void renderPage(cairo_t* cr, const Page& page, const RenderContext& ctx);
    void invalidateCache(const Page& page);
    void invalidateAllCaches();
};
