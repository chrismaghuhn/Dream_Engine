#version 450

layout(location = 0) out vec4 out_color;

void main() {
    const vec3 zenith = vec3(0.32, 0.52, 0.92);
    const vec3 horizon = vec3(0.62, 0.78, 0.95);
    const float t = clamp(gl_FragCoord.y / 800.0, 0.0, 1.0);
    const vec3 sky = mix(horizon, zenith, t);
    out_color = vec4(sky, 1.0);
    gl_FragDepth = 1.0;
}
