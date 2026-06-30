#include <cstdint>
#include <cstring>

namespace {

float bits_to_float(int bits) {
  const auto raw = static_cast<uint32_t>(bits);
  float value = 0.0f;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

int float_to_bits(float value) {
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(raw));
  return static_cast<int>(raw);
}

}  // namespace

extern "C" int cuper_verilator_fadd32(int a, int b) {
  return float_to_bits(bits_to_float(a) + bits_to_float(b));
}

extern "C" int cuper_verilator_fmul32(int a, int b) {
  return float_to_bits(bits_to_float(a) * bits_to_float(b));
}
