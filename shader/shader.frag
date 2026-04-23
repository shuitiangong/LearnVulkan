#version 450

layout(location = 0) in vec2 Texcoord;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    vec4 color;
} pc;

void main() {
    outColor = texture(texSampler, Texcoord) * pc.color;
}
