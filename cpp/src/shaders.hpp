// GLSL 3.3-core shader sources: a solid Lambert shader (flat tint or texture,
// two directional lights + ambient, spec §6.2-§6.3) and a flat line shader.
#pragma once

namespace animate {

inline const char* kSolidVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec3 vNormal;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
}
)";

inline const char* kSolidFrag = R"(#version 330 core
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;

uniform bool uUseTexture;
uniform sampler2D uTex;
uniform vec3 uBaseColor;

uniform vec3 uKeyDir;    // direction toward the key light
uniform vec3 uKeyColor;
uniform vec3 uFillDir;   // direction toward the fill light
uniform vec3 uFillColor;
uniform vec3 uAmbient;

void main() {
    vec3 N = normalize(vNormal);
    float keyd = max(dot(N, normalize(uKeyDir)), 0.0);
    float filld = max(dot(N, normalize(uFillDir)), 0.0);
    vec3 light = uAmbient + uKeyColor * keyd + uFillColor * filld;

    vec3 base = uUseTexture ? texture(uTex, vUV).rgb : uBaseColor;
    FragColor = vec4(base * light, 1.0);
}
)";

inline const char* kLineVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

inline const char* kLineFrag = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; }
)";

}  // namespace animate
