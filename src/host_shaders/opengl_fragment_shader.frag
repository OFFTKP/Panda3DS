#version 410 core

in vec3 v_tangent;
in vec3 v_normal;
in vec3 v_bitangent;
in vec4 v_colour;
in vec3 v_texcoord0;
in vec2 v_texcoord1;
in vec3 v_view;
in vec2 v_texcoord2;
flat in vec4 v_textureEnvColor[6];
flat in vec4 v_textureEnvBufferColor;

out vec4 fragColour;

// TEV uniforms
uniform uint u_textureEnvSource[6];
uniform uint u_textureEnvOperand[6];
uniform uint u_textureEnvCombiner[6];
uniform uint u_textureEnvScale[6];

// Depth control uniforms
uniform float u_depthScale;
uniform float u_depthOffset;
uniform bool u_depthmapEnable;

uniform sampler2D u_tex0;
uniform sampler2D u_tex1;
uniform sampler2D u_tex2;
uniform sampler2D u_tex_lighting_lut;

uniform uint u_picaRegs[0x200 - 0x48];

// Helper so that the implementation of u_pica_regs can be changed later
uint readPicaReg(uint reg_addr) { return u_picaRegs[reg_addr - 0x48u]; }

vec4 tevSources[16];
vec4 tevNextPreviousBuffer;
bool tevUnimplementedSourceFlag = false;

// Holds the enabled state of the lighting samples for various PICA configurations
// As explained in https://www.3dbrew.org/wiki/GPU/Internal_Registers#GPUREG_LIGHTING_CONFIG0
const bool samplerEnabled[9 * 7] = bool[9 * 7](
	// D0     D1     SP     FR     RB     RG     RR
	true,  false, true,  false, false, false, true,  // Configuration 0: D0, SP, RR
	false, false, true,  true,  false, false, true,  // Configuration 1: FR, SP, RR
	true,  true,  false, false, false, false, true,  // Configuration 2: D0, D1, RR
	true,  true,  false, true,  false, false, false, // Configuration 3: D0, D1, FR
	true,  true,  true,  false, true,  true,  true,  // Configuration 4: All except for FR
	true,  false, true,  true,  true,  true,  true,  // Configuration 5: All except for D1
	true,  true,  true,  true,  false, false, true,  // Configuration 6: All except for RB and RG
	false, false, false, false, false, false, false, // Configuration 7: Unused
	true,  true,  true,  true,  true,  true,  true   // Configuration 8: All
);

// OpenGL ES 1.1 reference pages for TEVs (this is what the PICA200 implements):
// https://registry.khronos.org/OpenGL-Refpages/es1.1/xhtml/glTexEnv.xml

vec4 tevFetchSource(uint src_id) {
	if (src_id >= 6u && src_id < 13u) {
		tevUnimplementedSourceFlag = true;
	}

	return tevSources[src_id];
}

vec4 tevGetColorAndAlphaSource(int tev_id, int src_id) {
	vec4 result;

	vec4 colorSource = tevFetchSource((u_textureEnvSource[tev_id] >> (src_id * 4)) & 15u);
	vec4 alphaSource = tevFetchSource((u_textureEnvSource[tev_id] >> (src_id * 4 + 16)) & 15u);

	uint colorOperand = (u_textureEnvOperand[tev_id] >> (src_id * 4)) & 15u;
	uint alphaOperand = (u_textureEnvOperand[tev_id] >> (12 + src_id * 4)) & 7u;

	// TODO: figure out what the undocumented values do
	switch (colorOperand) {
		case 0u: result.rgb = colorSource.rgb; break;             // Source color
		case 1u: result.rgb = 1.0 - colorSource.rgb; break;       // One minus source color
		case 2u: result.rgb = vec3(colorSource.a); break;         // Source alpha
		case 3u: result.rgb = vec3(1.0 - colorSource.a); break;   // One minus source alpha
		case 4u: result.rgb = vec3(colorSource.r); break;         // Source red
		case 5u: result.rgb = vec3(1.0 - colorSource.r); break;   // One minus source red
		case 8u: result.rgb = vec3(colorSource.g); break;         // Source green
		case 9u: result.rgb = vec3(1.0 - colorSource.g); break;   // One minus source green
		case 12u: result.rgb = vec3(colorSource.b); break;        // Source blue
		case 13u: result.rgb = vec3(1.0 - colorSource.b); break;  // One minus source blue
		default: break;
	}

	// TODO: figure out what the undocumented values do
	switch (alphaOperand) {
		case 0u: result.a = alphaSource.a; break;        // Source alpha
		case 1u: result.a = 1.0 - alphaSource.a; break;  // One minus source alpha
		case 2u: result.a = alphaSource.r; break;        // Source red
		case 3u: result.a = 1.0 - alphaSource.r; break;  // One minus source red
		case 4u: result.a = alphaSource.g; break;        // Source green
		case 5u: result.a = 1.0 - alphaSource.g; break;  // One minus source green
		case 6u: result.a = alphaSource.b; break;        // Source blue
		case 7u: result.a = 1.0 - alphaSource.b; break;  // One minus source blue
		default: break;
	}

	return result;
}

vec4 tevCalculateCombiner(int tev_id) {
	vec4 source0 = tevGetColorAndAlphaSource(tev_id, 0);
	vec4 source1 = tevGetColorAndAlphaSource(tev_id, 1);
	vec4 source2 = tevGetColorAndAlphaSource(tev_id, 2);

	uint colorCombine = u_textureEnvCombiner[tev_id] & 15u;
	uint alphaCombine = (u_textureEnvCombiner[tev_id] >> 16) & 15u;

	vec4 result = vec4(1.0);

	// TODO: figure out what the undocumented values do
	switch (colorCombine) {
		case 0u: result.rgb = source0.rgb; break;                                            // Replace
		case 1u: result.rgb = source0.rgb * source1.rgb; break;                              // Modulate
		case 2u: result.rgb = min(vec3(1.0), source0.rgb + source1.rgb); break;              // Add
		case 3u: result.rgb = clamp(source0.rgb + source1.rgb - 0.5, 0.0, 1.0); break;       // Add signed
		case 4u: result.rgb = mix(source1.rgb, source0.rgb, source2.rgb); break;             // Interpolate
		case 5u: result.rgb = max(source0.rgb - source1.rgb, 0.0); break;                    // Subtract
		case 6u: result.rgb = vec3(4.0 * dot(source0.rgb - 0.5, source1.rgb - 0.5)); break;  // Dot3 RGB
		case 7u: result = vec4(4.0 * dot(source0.rgb - 0.5, source1.rgb - 0.5)); break;      // Dot3 RGBA
		case 8u: result.rgb = min(source0.rgb * source1.rgb + source2.rgb, 1.0); break;      // Multiply then add
		case 9u: result.rgb = min((source0.rgb + source1.rgb) * source2.rgb, 1.0); break;    // Add then multiply
		default: break;
	}

	if (colorCombine != 7u) {  // The color combiner also writes the alpha channel in the "Dot3 RGBA" mode.
		// TODO: figure out what the undocumented values do
		// TODO: test if the alpha combiner supports all the same modes as the color combiner.
		switch (alphaCombine) {
			case 0u: result.a = source0.a; break;                                      // Replace
			case 1u: result.a = source0.a * source1.a; break;                          // Modulate
			case 2u: result.a = min(1.0, source0.a + source1.a); break;                // Add
			case 3u: result.a = clamp(source0.a + source1.a - 0.5, 0.0, 1.0); break;   // Add signed
			case 4u: result.a = mix(source1.a, source0.a, source2.a); break;           // Interpolate
			case 5u: result.a = max(0.0, source0.a - source1.a); break;                // Subtract
			case 8u: result.a = min(1.0, source0.a * source1.a + source2.a); break;    // Multiply then add
			case 9u: result.a = min(1.0, (source0.a + source1.a) * source2.a); break;  // Add then multiply
			default: break;
		}
	}

	result.rgb *= float(1 << (u_textureEnvScale[tev_id] & 3u));
	result.a *= float(1 << ((u_textureEnvScale[tev_id] >> 16) & 3u));

	return result;
}

#define D0_LUT 0u
#define D1_LUT 1u
#define SP_LUT 2u
#define FR_LUT 3u
#define RB_LUT 4u
#define RG_LUT 5u
#define RR_LUT 6u

uint GPUREG_LIGHTING_CONFIG0;
uint GPUREG_LIGHTING_CONFIG1;
uint GPUREG_LIGHTING_LUTINPUT_SELECT;
uint GPUREG_LIGHTING_LUTINPUT_SCALE;
uint GPUREG_LIGHTING_LUTINPUT_ABS;
uint GPUREG_LIGHTi_CONFIG;
bool error_unimpl;
vec4 unimpl_color;

float lutLookup(uint lut, int index) {
	return texelFetch(u_tex_lighting_lut, ivec2(index, lut), 0).r;
}

vec3 regToColor(uint reg) {
	// Normalization scale to convert from [0...255] to [0.0...1.0]
	const float scale = 1.0 / 255.0;

	return scale * vec3(float(bitfieldExtract(reg, 20, 8)), float(bitfieldExtract(reg, 10, 8)), float(bitfieldExtract(reg, 00, 8)));
}

// Convert an arbitrary-width floating point literal to an f32
float decodeFP(uint hex, uint E, uint M) {
	uint width = M + E + 1u;
	uint bias = 128u - (1u << (E - 1u));
	uint exponent = (hex >> M) & ((1u << E) - 1u);
	uint mantissa = hex & ((1u << M) - 1u);
	uint sign = (hex >> (E + M)) << 31u;

	if ((hex & ((1u << (width - 1u)) - 1u)) != 0u) {
		if (exponent == (1u << E) - 1u)
			exponent = 255u;
		else
			exponent += bias;
		hex = sign | (mantissa << (23u - M)) | (exponent << 23u);
	} else {
		hex = sign;
	}

	return uintBitsToFloat(hex);
}

bool isSamplerEnabled(uint environment_id, uint lut_id) {
	return samplerEnabled[7 * environment_id + lut_id];
}

float fixed1_1_11ToFloat(uint fixed_num) {
	uint sign = (fixed_num >> 12u) & 1u;
	float integer_part = float(fixed_num >> 11u);
	float fraction_part = float(fixed_num & 0x7FFu) / 2048.0;
	float result = integer_part + fraction_part;
	if (sign == 1u) result = -result;
	return result;
}

float lightLutLookup(uint environment_id, uint lut_id, uint light_id, vec3 normal, vec3 view, vec3 light_vector, vec3 half_vector) {
	uint lut_index;
	// lut_id is one of these values
	// 0 	D0
	// 1 	D1
	// 2 	SP
	// 3 	FR
	// 4 	RB
	// 5 	RG
	// 6 	RR 

	// lut_index on the other hand represents the actual index of the LUT in the texture
	// u_tex_lighting_lut has 24 LUTs and they are used like so:
	// 0 		D0
	// 1 		D1
	// 2 		is missing because SP uses LUTs 8-15
	// 3 		FR
	// 4 		RB
	// 5 		RG
	// 6 		RR
	// 8-15 	SP0-7
	// 16-23 	DA0-7, but this is not handled in this function as the lookup is a bit different

	int bit_in_config1;
	if (lut_id == SP_LUT) {
		// These are the spotlight attenuation LUTs
		bit_in_config1 = 8 + int(light_id & 7u);
		lut_index = 8u + light_id;
	} else if (lut_id <= 6) {
		bit_in_config1 = 16 + int(lut_id);
		lut_index = lut_id;
	} else {
		error_unimpl = true;
	}

	// The light environment configuration controls which LUTs are available for use
	// If a LUT is not available in the selected configuration, its value will always read a constant 1.0 regardless of the enable state in GPUREG_LIGHTING_CONFIG1
	// If RR is enabled but not RG or RB, the output of RR is used for the three components; Red, Green and Blue.
	bool current_sampler_enabled = isSamplerEnabled(environment_id, lut_id); // 7 luts per environment

	if (!current_sampler_enabled || (bitfieldExtract(GPUREG_LIGHTING_CONFIG1, bit_in_config1, 1) != 0u)) {
		return 1.0;
	}

	uint scale_id = bitfieldExtract(GPUREG_LIGHTING_LUTINPUT_SCALE, int(lut_id) * 4, 3);
	float scale = float(1u << scale_id);
	if (scale_id >= 6u) scale /= 256.0;

	float delta = 1.0;
	uint input_id = bitfieldExtract(GPUREG_LIGHTING_LUTINPUT_SELECT, int(lut_id) * 4, 3);
	switch (input_id) {
		case 0u: {
			delta = dot(normal, half_vector);
			break;
		}
		case 1u: {
			delta = dot(view, half_vector);
			break;
		}
		case 2u: {
			delta = dot(normal, view);
			break;
		}
		case 3u: {
			delta = dot(light_vector, normal);
			break;
		}
		case 4u: {
			uint GPUREG_LIGHTi_SPOTDIR_LOW = readPicaReg(0x0146u + 0x10u * light_id);
			uint GPUREG_LIGHTi_SPOTDIR_HIGH = readPicaReg(0x0147u + 0x10u * light_id);

			// These are fixed point 1.1.11 values, so we need to convert them to float
			float x = fixed1_1_11ToFloat(bitfieldExtract(GPUREG_LIGHTi_SPOTDIR_LOW, 0, 13));
			float y = fixed1_1_11ToFloat(bitfieldExtract(GPUREG_LIGHTi_SPOTDIR_LOW, 16, 13));
			float z = fixed1_1_11ToFloat(bitfieldExtract(GPUREG_LIGHTi_SPOTDIR_HIGH, 0, 13));
			vec3 spotlight_vector = normalize(vec3(x, y, z));
			delta = dot(-light_vector, spotlight_vector);  // -L dot P (aka Spotlight aka SP);
			break;
		}
		case 5u: {
			delta = 1.0;  // TODO: cos <greek symbol> (aka CP);
			error_unimpl = true;
			break;
		}
		default: {
			delta = 1.0;
			error_unimpl = true;
			break;
		}
	}

	if (bitfieldExtract(GPUREG_LIGHTING_LUTINPUT_ABS, 1 + 4 * int(lut_id), 1) == 0u) {
		delta = abs(delta);
		int index = int(clamp(floor(delta * 255.0), 0.f, 255.f));
		return lutLookup(lut_index, index) * scale;
	} else {
		int index = int(clamp(floor(delta * 128.0), -128.f, 127.f));
		if (index < 0) index += 256;
		return lutLookup(lut_index, index) * scale;
	}
}

// Implements the following algorthm: https://mathb.in/26766
void calcLighting(out vec4 primary_color, out vec4 secondary_color) {
	error_unimpl = false;
	unimpl_color = vec4(1.0, 0.0, 1.0, 1.0);

	// Quaternions describe a transformation from surface-local space to eye space.
	// In surface-local space, by definition (and up to permutation) the normal vector is (0,0,1),
	// the tangent vector is (1,0,0), and the bitangent vector is (0,1,0).
	vec3 normal = normalize(v_normal);
	vec3 tangent = normalize(v_tangent); // TODO: these dont have to be normalized as they are normalized in the vshader?
	vec3 bitangent = normalize(v_bitangent);
	vec3 view = normalize(v_view); // TODO: except you

	uint GPUREG_LIGHTING_ENABLE = readPicaReg(0x008Fu);
	if (bitfieldExtract(GPUREG_LIGHTING_ENABLE, 0, 1) == 0u) {
		primary_color = secondary_color = vec4(0.0);
		return;
	}

	uint GPUREG_LIGHTING_AMBIENT = readPicaReg(0x01C0u);
	uint GPUREG_LIGHTING_NUM_LIGHTS = (readPicaReg(0x01C2u) & 0x7u) + 1u;
	uint GPUREG_LIGHTING_LIGHT_PERMUTATION = readPicaReg(0x01D9u);

	primary_color = vec4(vec3(0.0), 1.0);
	secondary_color = vec4(vec3(0.0), 1.0);

	primary_color.rgb += regToColor(GPUREG_LIGHTING_AMBIENT);

	uint GPUREG_LIGHTING_LUTINPUT_SCALE = readPicaReg(0x01D2u);
	GPUREG_LIGHTING_LUTINPUT_ABS = readPicaReg(0x01D0u);
	GPUREG_LIGHTING_LUTINPUT_SELECT = readPicaReg(0x01D1u);
	GPUREG_LIGHTING_CONFIG0 = readPicaReg(0x01C3u);
	GPUREG_LIGHTING_CONFIG1 = readPicaReg(0x01C4u);

	uint lookup_config = bitfieldExtract(GPUREG_LIGHTING_CONFIG0, 4, 4);
	vec4 diffuse_sum = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 specular_sum = vec4(0.0, 0.0, 0.0, 1.0);

	for (uint i = 0u; i < GPUREG_LIGHTING_NUM_LIGHTS; i++) {
		uint light_id = bitfieldExtract(GPUREG_LIGHTING_LIGHT_PERMUTATION, int(i * 3u), 3);

		uint GPUREG_LIGHTi_SPECULAR0 = readPicaReg(0x0140u + 0x10u * light_id);
		uint GPUREG_LIGHTi_SPECULAR1 = readPicaReg(0x0141u + 0x10u * light_id);
		uint GPUREG_LIGHTi_DIFFUSE = readPicaReg(0x0142u + 0x10u * light_id);
		uint GPUREG_LIGHTi_AMBIENT = readPicaReg(0x0143u + 0x10u * light_id);
		uint GPUREG_LIGHTi_VECTOR_LOW = readPicaReg(0x0144u + 0x10u * light_id);
		uint GPUREG_LIGHTi_VECTOR_HIGH = readPicaReg(0x0145u + 0x10u * light_id);
		GPUREG_LIGHTi_CONFIG = readPicaReg(0x0149u + 0x10u * light_id);

		vec3 light_vector = normalize(vec3(
			decodeFP(bitfieldExtract(GPUREG_LIGHTi_VECTOR_LOW, 0, 16), 5u, 10u), decodeFP(bitfieldExtract(GPUREG_LIGHTi_VECTOR_LOW, 16, 16), 5u, 10u),
			decodeFP(bitfieldExtract(GPUREG_LIGHTi_VECTOR_HIGH, 0, 16), 5u, 10u)
		));

		vec3 half_vector;

		// Positional Light
		if (bitfieldExtract(GPUREG_LIGHTi_CONFIG, 0, 1) == 0u) {
			// error_unimpl = true;
			half_vector = normalize(normalize(light_vector + v_view) + view);
		}

		// Directional light
		else {
			half_vector = normalize(normalize(light_vector) + view);
		}

		float NdotL = dot(normal, light_vector);  // N dot Li

		// Two sided diffuse
		if (bitfieldExtract(GPUREG_LIGHTi_CONFIG, 1, 1) == 0u)
			NdotL = max(0.0, NdotL);
		else
			NdotL = abs(NdotL);

		uint environment_id = bitfieldExtract(GPUREG_LIGHTING_CONFIG0, 4, 4);
		float spotlight_attenuation = lightLutLookup(environment_id, SP_LUT, light_id, normal, view, light_vector, half_vector);
		float distance_attenuation = 1.0;
		float specular0_distribution = lightLutLookup(environment_id, D0_LUT, light_id, normal, view, light_vector, half_vector);
		float specular1_distribution = lightLutLookup(environment_id, D1_LUT, light_id, normal, view, light_vector, half_vector);
		vec3 reflected_color;
		reflected_color.r = lightLutLookup(environment_id, RR_LUT, light_id, normal, view, light_vector, half_vector);
		
		if (isSamplerEnabled(environment_id, RG_LUT)) {
			reflected_color.g = lightLutLookup(environment_id, RG_LUT, light_id, normal, view, light_vector, half_vector);
		} else {
			reflected_color.g = reflected_color.r;
		}

		if (isSamplerEnabled(environment_id, RB_LUT)) {
			reflected_color.b = lightLutLookup(environment_id, RB_LUT, light_id, normal, view, light_vector, half_vector);
		} else {
			reflected_color.b = reflected_color.r;
		}

		vec3 specular0 = regToColor(GPUREG_LIGHTi_SPECULAR0) * specular0_distribution;
		vec3 specular1 = regToColor(GPUREG_LIGHTi_SPECULAR1) * specular1_distribution * reflected_color;

		float geometric_factor;
		bool use_geo_0 = bitfieldExtract(GPUREG_LIGHTi_CONFIG, 2, 1) == 1u;
		bool use_geo_1 = bitfieldExtract(GPUREG_LIGHTi_CONFIG, 3, 1) == 1u;
		if (use_geo_0 || use_geo_1) {
			geometric_factor = dot(half_vector, half_vector);
			geometric_factor = geometric_factor == 0.0 ? 0.0 : min(NdotL / geometric_factor, 1.0);
		}
		specular0 *= use_geo_0 ? geometric_factor : 1.0;
		specular1 *= use_geo_1 ? geometric_factor : 1.0;

		if (i == GPUREG_LIGHTING_NUM_LIGHTS - 1u) {
			uint fresnel_output1 = bitfieldExtract(GPUREG_LIGHTING_CONFIG0, 2, 1);
			uint fresnel_output2 = bitfieldExtract(GPUREG_LIGHTING_CONFIG0, 3, 1);
			float fresnel_factor = lightLutLookup(environment_id, FR_LUT, light_id, normal, view, light_vector, half_vector);
			if (fresnel_output1 == 1u) {
				diffuse_sum.a = fresnel_factor;
			}

			if (fresnel_output2 == 1u) {
				specular_sum.a = fresnel_factor;
			}
		}

		float light_factor = distance_attenuation * spotlight_attenuation;
		diffuse_sum.rgb += light_factor * (regToColor(GPUREG_LIGHTi_AMBIENT) + regToColor(GPUREG_LIGHTi_DIFFUSE) * NdotL);
		specular_sum.rgb += light_factor * (specular0 + specular1);
	}

	vec4 global_ambient = vec4(regToColor(GPUREG_LIGHTING_AMBIENT), 1.0);
	primary_color = clamp(global_ambient + diffuse_sum, vec4(0.0), vec4(1.0));
	secondary_color = clamp(specular_sum, vec4(0.0), vec4(1.0));

	if (error_unimpl) {
		secondary_color = primary_color = unimpl_color;
	}
}

void main() {
	// TODO: what do invalid sources and disabled textures read as?
	// And what does the "previous combiner" source read initially?
	tevSources[0] = v_colour;  // Primary/vertex color
	calcLighting(tevSources[1], tevSources[2]);

	uint textureConfig = readPicaReg(0x80u);
	vec2 tex2UV = (textureConfig & (1u << 13)) != 0u ? v_texcoord1 : v_texcoord2;

	if ((textureConfig & 1u) != 0u) tevSources[3] = texture(u_tex0, v_texcoord0.xy);
	if ((textureConfig & 2u) != 0u) tevSources[4] = texture(u_tex1, v_texcoord1);
	if ((textureConfig & 4u) != 0u) tevSources[5] = texture(u_tex2, tex2UV);
	tevSources[13] = vec4(0.0);  // Previous buffer
	tevSources[15] = v_colour;   // Previous combiner

	tevNextPreviousBuffer = v_textureEnvBufferColor;
	uint textureEnvUpdateBuffer = readPicaReg(0xE0u);

	for (int i = 0; i < 6; i++) {
		tevSources[14] = v_textureEnvColor[i];  // Constant color
		tevSources[15] = tevCalculateCombiner(i);
		tevSources[13] = tevNextPreviousBuffer;

		if (i < 4) {
			if ((textureEnvUpdateBuffer & (0x100u << i)) != 0u) {
				tevNextPreviousBuffer.rgb = tevSources[15].rgb;
			}

			if ((textureEnvUpdateBuffer & (0x1000u << i)) != 0u) {
				tevNextPreviousBuffer.a = tevSources[15].a;
			}
		}
	}

	fragColour = tevSources[15];

	if (tevUnimplementedSourceFlag) {
		// fragColour = vec4(1.0, 0.0, 1.0, 1.0);
	}
	// fragColour.rg = texture(u_tex_lighting_lut,vec2(gl_FragCoord.x/200.,float(int(gl_FragCoord.y/2)%24))).rr;

	// Get original depth value by converting from [near, far] = [0, 1] to [-1, 1]
	// We do this by converting to [0, 2] first and subtracting 1 to go to [-1, 1]
	float z_over_w = gl_FragCoord.z * 2.0f - 1.0f;
	float depth = z_over_w * u_depthScale + u_depthOffset;

	if (!u_depthmapEnable)  // Divide z by w if depthmap enable == 0 (ie using W-buffering)
		depth /= gl_FragCoord.w;

	// Write final fragment depth
	gl_FragDepth = depth;

	// Perform alpha test
	uint alphaControl = readPicaReg(0x104u);
	if ((alphaControl & 1u) != 0u) {  // Check if alpha test is on
		uint func = (alphaControl >> 4u) & 7u;
		float reference = float((alphaControl >> 8u) & 0xffu) / 255.0;
		float alpha = fragColour.a;

		switch (func) {
			case 0u: discard;  // Never pass alpha test
			case 1u: break;    // Always pass alpha test
			case 2u:           // Pass if equal
				if (alpha != reference) discard;
				break;
			case 3u:  // Pass if not equal
				if (alpha == reference) discard;
				break;
			case 4u:  // Pass if less than
				if (alpha >= reference) discard;
				break;
			case 5u:  // Pass if less than or equal
				if (alpha > reference) discard;
				break;
			case 6u:  // Pass if greater than
				if (alpha <= reference) discard;
				break;
			case 7u:  // Pass if greater than or equal
				if (alpha < reference) discard;
				break;
		}
	}
}
