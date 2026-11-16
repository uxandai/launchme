// Direct2D's arithmetic composite on premultiplied values: k1*s*d + k2*s + k3*d + k4, clamped, with
// d the first input (destination) and s the second (source); transparent outside an input.
#include <flutter/runtime_effect.glsl>

uniform vec4 u_destination; // The first input's left, top, width, height in grid pixels.
uniform vec4 u_source;      // The second input's.
uniform vec4 u_flags;       // x, y: 1 samples the first (second) input upside down; z, w: 1 when it exists.
uniform vec4 u_k;           // k1, k2, k3, k4.
uniform sampler2D u_first;
uniform sampler2D u_second;

out vec4 frag_color;

vec4 sampleAt(sampler2D image, vec4 area, float flip, float present, vec2 pixel) {
  if (present < 0.5 || pixel.x < area.x || pixel.y < area.y || pixel.x >= area.x + area.z || pixel.y >= area.y + area.w) {
    return vec4(0.0);
  }
  vec2 uv = (pixel - area.xy + 0.5) / area.zw;
  if (flip > 0.5) {
    uv.y = 1.0 - uv.y;
  }
  return texture(image, uv);
}

void main() {
  vec2 p = floor(FlutterFragCoord());
  vec4 d = sampleAt(u_first, u_destination, u_flags.x, u_flags.z, p);
  vec4 s = sampleAt(u_second, u_source, u_flags.y, u_flags.w, p);
  vec4 result = clamp(u_k.x * s * d + u_k.y * s + u_k.z * d + u_k.w, 0.0, 1.0);
  frag_color = vec4(min(result.rgb, vec3(result.a)), result.a);
}
