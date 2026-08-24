#version 450

layout(location = 0) in ivec4 inPos;
layout(location = 1) in uvec4 inMeta; // u, v, tex, shade

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vShade;
layout(location = 2) out float vDist;

layout(push_constant) uniform Pc {
    vec4 origin;
} pc;

layout(set = 0, binding = 0) uniform Ubo {
    mat4 viewProj;
    vec4 camPos;
    vec4 fogParams; // x start, y end, z skyR, w skyG
    vec4 misc;      // x skyB, y atlasPx, z tilesX, w tilePx
    vec4 day;       // x daylight 0..1, yzw sun dir
} ubo;

void main() {
    vec3 p = vec3(inPos.xyz);
    vec3 wp = p + pc.origin.xyz;
    gl_Position = ubo.viewProj * vec4(wp, 1.0);

    int tex = int(inMeta.z);
    vec2 tileUV = vec2(float(inMeta.x), float(inMeta.y));
    vec2 tileIdx = vec2(float(tex % int(ubo.misc.z)), float(tex / int(ubo.misc.z)));
    vec2 px = tileIdx * ubo.misc.w + tileUV + 0.5;
    vUV = px / ubo.misc.y;
    vShade = float(inMeta.w) / 255.0;
    vDist = length(wp - ubo.camPos.xyz);
}
