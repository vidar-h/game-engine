#pragma once

#include <cstdint>
#include <spdlog/spdlog.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

#define EG_FATAL(fmt, ...)                                                     \
  do {                                                                         \
    SPDLOG_ERROR(fmt __VA_OPT__(, ) __VA_ARGS__);                              \
    SPDLOG_ERROR("Fatal condition reached, exiting");                          \
    exit(1);                                                                   \
  } while (0)

#define EG_ASSERT(cond)                                                        \
  do {                                                                         \
    if (!(cond)) {                                                             \
      SPDLOG_ERROR("Assertion failure: {}", #cond);                            \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
