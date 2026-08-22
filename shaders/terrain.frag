#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vShade;
layout(location = 2) in float vDist;

layout(set = 0, binding = 1) uniform sampler2D atlas;
layout(set = 0, binding = 0) uniform Ubo {
    mat4 viewProj;
    vec4 camPos;
    vec4 fogParams;
    vec4 misc;
    vec4 day;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = texture(atlas, vUV);
    if (c.a < 0.3) discard;
    vec3 col = c.rgb * vShade * ubo.day.x;
    float fog = clamp((vDist - ubo.fogParams.x) / (ubo.fogParams.y - ubo.fogParams.x), 0.0, 1.0);
    vec3 sky = mix(vec3(0.10, 0.13, 0.24), vec3(ubo.fogParams.z, ubo.fogParams.w, ubo.misc.x), ubo.day.x);
    col = mix(col, sky, fog);
    outColor = vec4(col, 1.0);
}
