#version 450
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform UBO {
    vec3 color;
} ubo;
void main() {
    fragColor = vec4(ubo.color, 1.0);
}
