#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vTint;

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = texture(atlas, vUV);
    outColor = c * vTint;
}
