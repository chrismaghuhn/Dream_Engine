#version 450

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
} pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in uint in_color_rgba;

layout(location = 0) out vec4 v_color;

void main() {
    float r = float((in_color_rgba >> 0)  & 0xFFu) / 255.0;
    float g = float((in_color_rgba >> 8)  & 0xFFu) / 255.0;
    float b = float((in_color_rgba >> 16) & 0xFFu) / 255.0;
    float a = float((in_color_rgba >> 24) & 0xFFu) / 255.0;
    v_color = vec4(r, g, b, a);
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
}
