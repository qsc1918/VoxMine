#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Ubo {
    vec4 horizon;
    vec4 zenith;
} ubo;

void main() {
    vec3 c = mix(ubo.zenith.rgb, ubo.horizon.rgb, clamp(vUV.y, 0.0, 1.0));
    outColor = vec4(c, 1.0);
}
