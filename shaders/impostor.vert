#version 450

layout(location = 0) in vec3 in_pos;

layout(set = 0, binding = 0) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 render_origin;
    vec4 ambient_fog;
} frame;

layout(push_constant) uniform Push {
    vec3 model_translation;
    float min_y;
    float max_y;
    vec3 color;
    float pad;
} push;

layout(location = 0) out vec3 v_color;

void main() {
    const float height = max(push.max_y - push.min_y, 0.001);
    const vec3 world = push.model_translation
        + vec3(in_pos.x * 32.0, in_pos.y * height + push.min_y, in_pos.z * 32.0);
    gl_Position = frame.proj * frame.view * vec4(world, 1.0);
    v_color = push.color;
}