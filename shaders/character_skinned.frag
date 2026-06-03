#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 2) uniform sampler2D albedo;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = texture(albedo, inUV);

    // Simple directional light from above-front.
    float diffuse = max(dot(normalize(inNormal), normalize(vec3(0.4, 1.0, 0.6))), 0.0);
    float ambient = 0.35;
    float light   = ambient + (1.0 - ambient) * diffuse;

    outColor = vec4(base.rgb * light, base.a);
}
