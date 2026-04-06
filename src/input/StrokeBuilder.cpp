#include "../document/Stroke.h"
#include "../util/SafeArithmetic.h"
#include <memory>
class StrokeBuilder {
public:
    void begin(const ToolProperties& p, const StrokePoint& pt) {}
    void addPoint(const StrokePoint& pt) {}
    std::shared_ptr<Stroke> finalize() { return nullptr; }
    bool isActive() const { return false; }
};
