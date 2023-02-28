#include "PICA/gpu.hpp"
#include "PICA/float_types.hpp"
#include "PICA/regs.hpp"
#include <cstdio>

using namespace Floats;

GPU::GPU(Memory& mem) : mem(mem), renderer(*this, regs) {
	vram = new u8[vramSize];
}

void GPU::reset() {
	regs.fill(0);
	shaderUnit.reset();
	std::memset(vram, 0, vramSize);

	totalAttribCount = 0;
	fixedAttribMask = 0;
	fixedAttribIndex = 0;
	fixedAttribCount = 0;
	fixedAttrBuff.fill(0);

	for (auto& e : attributeInfo) {
		e.offset = 0;
		e.size = 0;
		e.config1 = 0;
		e.config2 = 0;
	}

	renderer.reset();
}

void GPU::drawArrays(bool indexed) {
	if (indexed)
		drawArrays<true>();
	else
		drawArrays<false>();
}

template <bool indexed>
void GPU::drawArrays() {
	// Base address for vertex attributes
	// The vertex base is always on a quadword boundary because the PICA does weird alignment shit any time possible
	const u32 vertexBase = ((regs[PICAInternalRegs::VertexAttribLoc] >> 1) & 0xfffffff) * 16;
	const u32 vertexCount = regs[PICAInternalRegs::VertexCountReg]; // Total # of vertices to transfer

	// Configures the type of primitive and the number of vertex shader outputs
	const u32 primConfig = regs[PICAInternalRegs::PrimitiveConfig];
	const u32 primType = (primConfig >> 8) & 3;
	if (primType != 0 && primType != 1) Helpers::panic("[PICA] Tried to draw unimplemented shape %d\n", primType);
	if (vertexCount > Renderer::vertexBufferSize) Helpers::panic("[PICA] vertexCount > vertexBufferSize");

	if ((primType == 0 && vertexCount % 3) || (primType == 1 && vertexCount < 3)) {
		Helpers::panic("Invalid vertex count for primitive. Type: %d, vert count: %d\n", primType, vertexCount);
	}

	Vertex vertices[Renderer::vertexBufferSize];

	// Get the configuration for the index buffer, used only for indexed drawing
	u32 indexBufferConfig = regs[PICAInternalRegs::IndexBufferConfig];
	u32 indexBufferPointer = vertexBase + (indexBufferConfig & 0xfffffff);
	bool shortIndex = (indexBufferConfig >> 31) & 1; // Indicates whether vert indices are 16-bit or 8-bit

	// Stuff the global attribute config registers in one u64 to make attr parsing easier
	// TODO: Cache this when the vertex attribute format registers are written to 
	u64 vertexCfg = u64(regs[PICAInternalRegs::AttribFormatLow]) | (u64(regs[PICAInternalRegs::AttribFormatHigh]) << 32);

	if constexpr (!indexed) {
		u32 offset = regs[PICAInternalRegs::VertexOffsetReg];
		log("PICA::DrawArrays(vertex count = %d, vertexOffset = %d)\n", vertexCount, offset);
	} else {
		log("PICA::DrawElements(vertex count = %d, index buffer config = %08X)\n", vertexCount, indexBufferConfig);
	}

	// Total number of input attributes to shader. Differs between GS and VS. Currently stubbed to the VS one, as we don't have geometry shaders.
	const u32 inputAttrCount = (regs[PICAInternalRegs::VertexShaderInputBufferCfg] & 0xf) + 1;
	const u64 inputAttrCfg = getVertexShaderInputConfig();
		
	for (u32 i = 0; i < vertexCount; i++) {
		u32 vertexIndex; // Index of the vertex in the VBO

		if constexpr (!indexed) {
			vertexIndex = i + regs[PICAInternalRegs::VertexOffsetReg];
		} else {
			if (shortIndex) {
				auto ptr = getPointerPhys<u16>(indexBufferPointer);
				vertexIndex = *ptr; // TODO: This is very unsafe
				indexBufferPointer += 2;
			} else {
				auto ptr = getPointerPhys<u8>(indexBufferPointer);
				vertexIndex = *ptr; // TODO: This is also very unsafe
				indexBufferPointer += 1;
			}
		}

		int attrCount = 0;
		int buffer = 0; // Vertex buffer index for non-fixed attributes

		while (attrCount < totalAttribCount) {
			// Check if attribute is fixed or not
			if (fixedAttribMask & (1 << attrCount)) { // Fixed attribute
				vec4f& fixedAttr = shaderUnit.vs.fixedAttributes[attrCount]; // TODO: Is this how it works?
				vec4f& inputAttr = currentAttributes[attrCount];
				std::memcpy(&inputAttr, &fixedAttr, sizeof(vec4f)); // Copy fixed attr to input attr
				attrCount++;
			} else { // Non-fixed attribute
				auto& attr = attributeInfo[buffer]; // Get information for this attribute
				u64 attrCfg = attr.getConfigFull(); // Get config1 | (config2 << 32)
				u32 attrAddress = vertexBase + attr.offset + (vertexIndex * attr.size);

				for (int j = 0; j < attr.componentCount; j++) {
					uint index = (attrCfg >> (j * 4)) & 0xf; // Get index of attribute in vertexCfg
					if (index >= 12) Helpers::panic("[PICA] Vertex attribute used as padding");

					u32 attribInfo = (vertexCfg >> (index * 4)) & 0xf;
					u32 attribType = attribInfo & 0x3; //  Type of attribute(sbyte/ubyte/short/float)
					u32 size = (attribInfo >> 2) + 1; // Total number of components

					//printf("vertex_attribute_strides[%d] = %d\n", attrCount, attr.size);
					vec4f& attribute = currentAttributes[attrCount];
					uint component; // Current component

					switch (attribType) {
						case 0: { // Signed byte
							s8* ptr = getPointerPhys<s8>(attrAddress);
							for (component = 0; component < size; component++) {
								float val = static_cast<float>(*ptr++);
								attribute[component] = f24::fromFloat32(val);
							}
							attrAddress += size * sizeof(s8);
							break;
						}

						case 1: { // Unsigned byte
							u8* ptr = getPointerPhys<u8>(attrAddress);
							for (component = 0; component < size; component++) {
								float val = static_cast<float>(*ptr++);
								attribute[component] = f24::fromFloat32(val);
							}
							attrAddress += size * sizeof(u8);
							break;
						}

						case 2: { // Short
							s16* ptr = getPointerPhys<s16>(attrAddress);
							for (component = 0; component < size; component++) {
								float val = static_cast<float>(*ptr++);
								attribute[component] = f24::fromFloat32(val);
							}
							attrAddress += size * sizeof(s16);
							break;
						}

						case 3: { // Float
							float* ptr = getPointerPhys<float>(attrAddress);
							for (component = 0; component < size; component++) {
								float val = *ptr++;
								attribute[component] = f24::fromFloat32(val);
							}
							attrAddress += size * sizeof(float);
							break;
						}

						default: Helpers::panic("[PICA] Unimplemented attribute type %d", attribType);
					}

					// Fill the remaining attribute lanes with default parameters (1.0 for alpha/w, 0.0) for everything else
					// Corgi does this although I'm not sure if it's actually needed for anything.
					// TODO: Find out
					while (component < 4) {
						attribute[component] = (component == 3) ? f24::fromFloat32(1.0) : f24::fromFloat32(0.0);
						component++;
					}

					attrCount++;
				}
				buffer++;
			}
		}

		// Before running the shader, the PICA maps the fetched attributes from the attribute registers to the shader input registers
		// Based on the SH_ATTRIBUTES_PERMUTATION registers.
		// Ie it might attribute #0 to v2, #1 to v7, etc
		for (int j = 0; j < totalAttribCount; j++) {
			const u32 mapping = (inputAttrCfg >> (j * 4)) & 0xf;
			std::memcpy(&shaderUnit.vs.inputs[mapping], &currentAttributes[j], sizeof(vec4f));
		}
		
		shaderUnit.vs.run();
		std::memcpy(&vertices[i].position, &shaderUnit.vs.outputs[0], sizeof(vec4f));
		std::memcpy(&vertices[i].colour, &shaderUnit.vs.outputs[1], sizeof(vec4f));
		std::memcpy(&vertices[i].UVs, &shaderUnit.vs.outputs[2], 2 * sizeof(f24));

		//printf("(x, y, z, w) = (%f, %f, %f, %f)\n", (double)vertices[i].position.x(), (double)vertices[i].position.y(), (double)vertices[i].position.z(), (double)vertices[i].position.w());
		//printf("(r, g, b, a) = (%f, %f, %f, %f)\n", (double)vertices[i].colour.r(), (double)vertices[i].colour.g(), (double)vertices[i].colour.b(), (double)vertices[i].colour.a());
		//printf("(u, v        )       = (%f, %f)\n", vertices[i].UVs.u(), vertices[i].UVs.v());
	}

	// The fourth type is meant to be "Geometry primitive". TODO: Find out what that is
	static constexpr std::array<OpenGL::Primitives, 4> primTypes = {
		OpenGL::Triangle, OpenGL::TriangleStrip, OpenGL::TriangleFan, OpenGL::Triangle
	};
	const auto shape = primTypes[primType];
	renderer.drawVertices(shape, vertices, vertexCount);
}

void GPU::fireDMA(u32 dest, u32 source, u32 size) {
	printf("[GPU] DMA of %08X bytes from %08X to %08X\n", size, source, dest);
	constexpr u32 vramStart = VirtualAddrs::VramStart;
	constexpr u32 vramSize = VirtualAddrs::VramSize;

	const u32 fcramStart = mem.getLinearHeapVaddr();
	constexpr u32 fcramSize = VirtualAddrs::FcramTotalSize;

	if (dest - vramStart >= vramSize || size > (vramSize - (dest - vramStart))) [[unlikely]] {
		Helpers::panic("GPU DMA does not target VRAM");
	}

	if (source - fcramStart >= fcramSize || size > (fcramSize - (dest - fcramStart))) {
		Helpers::panic("GPU DMA does not have FCRAM as its source");
	}

	// Valid, optimized FCRAM->VRAM DMA. TODO: Is VRAM->VRAM DMA allowed?
	u8* fcram = mem.getFCRAM();
	std::memcpy(&vram[dest - vramStart], &fcram[source - fcramStart], size);
}
