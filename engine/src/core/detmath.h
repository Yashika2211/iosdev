// Math with results that do not depend on the platform's libm. Only IEEE-exact
// operations are used (+ - * / sqrt floor), so native and wasm builds agree.
#pragma once
#include <bit>
#include <cmath>
#include <cstdint>

namespace jam::dm {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = 6.28318530717959f;

inline float wrap01(float x) { return x - std::floor(x); }

// sin(2*pi*turns) for any finite input. Taylor to x^9 after folding to a
// quarter turn; the error is about 4e-6.
inline float sinTurns(float turns) {
  float t = wrap01(turns);
  if (t > 0.5f) t -= 1.0f;
  if (t > 0.25f) t = 0.5f - t;
  else if (t < -0.25f) t = -0.5f - t;
  const float x = t * kTwoPi;
  const float x2 = x * x;
  return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f +
         x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
}

inline float cosTurns(float turns) { return sinTurns(turns + 0.25f); }

// tan(pi*x) for 0 <= x < 0.5, which is what filter coefficients need.
inline float tanPi(float x) { return sinTurns(x * 0.5f) / cosTurns(x * 0.5f); }

// 2^x. Polynomial on the fractional part, exponent written directly.
inline float pow2(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 127.0f) x = 127.0f;
  const float fl = std::floor(x);
  const float f = x - fl;
  const float m = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f +
                  f * (0.00961813f + f * 0.00133336f))));
  const auto e = static_cast<uint32_t>(static_cast<int32_t>(fl) + 127) << 23;
  return m * std::bit_cast<float>(e);
}

inline float exp(float x) { return pow2(x * 1.44269504f); }
inline float dbToGain(float db) { return pow2(db * 0.16609640f); }
inline float midiToHz(float note) { return 440.0f * pow2((note - 69.0f) / 12.0f); }

// Coefficient for a one-pole that decays by 1/e every `seconds`.
inline float decayCoef(float seconds, float sampleRate) {
  return exp(-1.0f / (seconds * sampleRate));
}

// The platforms differ in denormal handling, so feedback paths flush by hand.
inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }

// Smooth saturation that stays below 1.
inline float softClip(float x) { return x / std::sqrt(1.0f + x * x); }

}  // namespace jam::dm
