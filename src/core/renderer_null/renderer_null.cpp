#include "renderer_null/renderer_null.hpp"

RendererNull::RendererNull(GPU& gpu, const std::array<u32, regNum>& internalRegs) : Renderer(gpu, internalRegs) {}
RendererNull::~RendererNull() {}

void RendererNull::reset() {}
void RendererNull::display() {}
void RendererNull::initGraphicsContext() {}
void RendererNull::clearBuffer(u32 startAddress, u32 endAddress, u32 value, u32 control) {}
void RendererNull::displayTransfer(u32 inputAddr, u32 outputAddr, u32 inputSize, u32 outputSize, u32 flags) {}
void RendererNull::drawVertices(PICA::PrimType primType, std::span<const PICA::Vertex> vertices) {}
void RendererNull::screenshot(const std::string& name) {}