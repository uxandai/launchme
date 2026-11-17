// Tryb TV's background upscaler on Linux: a single-pass 5-tap Catmull-Rom bicubic filter (using
// hardware bilinear fetches on the positive inner lobe pair) with local neighbourhood deringing
// and contrast-adaptive sharpening (CAS) over the same 5 samples.
#include <flutter/runtime_effect.glsl>

uniform vec4 u_dest;   // Destination left, top, width, height in device pixels.
uniform vec4 u_source; // Source width, height, upside-down flag (1 = flip Y), opacity.
uniform sampler2D u_image;

out vec4 frag_color;

vec3 sampleAt(vec2 texel_pos) {
  vec2 uv = clamp(texel_pos, vec2(0.5), u_source.xy - 0.5) / u_source.xy;
  if (u_source.z > 0.5) {
    uv.y = 1.0 - uv.y;
  }
  return texture(u_image, uv).rgb;
}

void main() {
  vec2 uv = clamp((FlutterFragCoord().xy - u_dest.xy) / max(u_dest.zw, vec2(1.0)), vec2(0.0), vec2(1.0));
  vec2 tex_pos = uv * u_source.xy - 0.5;
  vec2 f = fract(tex_pos);
  vec2 tc = tex_pos - f + 0.5;

  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);

  vec2 w12 = w1 + w2;
  vec2 tc0 = tc - 1.0;
  vec2 tc12 = tc + w2 / w12;
  vec2 tc3 = tc + 2.0;

  vec3 c_mid   = sampleAt(vec2(tc12.x, tc12.y));
  vec3 c_left  = sampleAt(vec2(tc0.x,  tc12.y));
  vec3 c_right = sampleAt(vec2(tc3.x,  tc12.y));
  vec3 c_top   = sampleAt(vec2(tc12.x, tc0.y));
  vec3 c_bot   = sampleAt(vec2(tc12.x, tc3.y));

  float s_mid   = w12.x * w12.y;
  float s_left  = w0.x  * w12.y;
  float s_right = w3.x  * w12.y;
  float s_top   = w12.x * w0.y;
  float s_bot   = w12.x * w3.y;
  float w_sum = s_mid + s_left + s_right + s_top + s_bot;

  vec3 bicubic = (c_mid * s_mid + c_left * s_left + c_right * s_right + c_top * s_top + c_bot * s_bot) / max(w_sum, 1e-5);
  vec3 mn = min(c_mid, min(min(c_left, c_right), min(c_top, c_bot)));
  vec3 mx = max(c_mid, max(max(c_left, c_right), max(c_top, c_bot)));
  bicubic = clamp(bicubic, mn, mx);

  vec3 hit_min = min(mn, bicubic) / (4.0 * mx + 1e-5);
  vec3 hit_max = (1.0 - max(mx, bicubic)) / (4.0 * mn - 4.0 - 1e-5);
  vec3 lobe_rgb = max(-hit_min, hit_max);
  float lobe = max(-0.1875, min(max(lobe_rgb.r, max(lobe_rgb.g, lobe_rgb.b)), 0.0)) * 0.8;
  vec3 sharp = clamp((lobe * (c_left + c_right + c_top + c_bot) + bicubic) / (4.0 * lobe + 1.0), 0.0, 1.0);

  float a = clamp(u_source.w, 0.0, 1.0);
  frag_color = vec4(sharp * a, a);
}
