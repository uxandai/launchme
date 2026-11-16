// Direct2D's colour matrix in its straight-alpha mode: the 5x4 matrix applied to the premultiplied
// values as they are, the result kept a valid premultiplied colour (channels at most alpha).
#include <flutter/runtime_effect.glsl>

uniform vec4 u_input;  // The input's left, top, width, height in grid pixels.
uniform vec4 u_flags;  // x: 1 samples the input upside down; y: 1 clamps the result to 0..1.
uniform vec4 u_red;    // Weights of r, g, b, a in the output's red (Direct2D's _11, _21, _31, _41).
uniform vec4 u_green;
uniform vec4 u_blue;
uniform vec4 u_alpha;
uniform vec4 u_offset; // Added to r, g, b, a (_51.._54).
uniform sampler2D u_image;

out vec4 frag_color;

void main() {
  vec2 uv = (floor(FlutterFragCoord()) - u_input.xy + 0.5) / u_input.zw;
  if (u_flags.x > 0.5) {
    uv.y = 1.0 - uv.y;
  }
  vec4 c = texture(u_image, uv);
  vec4 result = vec4(dot(c, u_red), dot(c, u_green), dot(c, u_blue), dot(c, u_alpha)) + u_offset;
  if (u_flags.y > 0.5) {
    result = clamp(result, 0.0, 1.0);
  }
  float a = clamp(result.a, 0.0, 1.0);
  frag_color = vec4(clamp(min(result.rgb, vec3(a)), 0.0, 1.0), a);
}
