#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vTint;

layout(set = 0, binding = 0) uniform sampler2D menu;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(menu, vUV) * vTint;
}
