#version 450

layout(location = 0) in vec3 v_color;

layout(set = 0, binding = 0) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 render_origin;
    vec4 ambient_fog;
} frame;

layout(location = 0) out vec4 out_color;

void main() {
    const vec3 ambient = frame.ambient_fog.xyz;
    const float ambient_scale = max(ambient.x + ambient.y + ambient.z, 0.15);
    out_color = vec4(v_color * ambient_scale, 1.0);
}