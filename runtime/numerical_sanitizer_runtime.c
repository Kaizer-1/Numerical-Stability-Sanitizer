#include "NumericalSanitizer/NumericalSanitizer.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define NSAN_WEAK __attribute__((weak))
#else
#define NSAN_WEAK
#endif

typedef struct {
  const void *key;
  double value;
  unsigned char state;
} ShadowSlot;

typedef struct {
  const void *byte_addr;
  const void *base_addr;
  unsigned char offset;
  unsigned char state;
} ShadowByteSlot;

enum {
  NSAN_SLOT_EMPTY = 0,
  NSAN_SLOT_KNOWN = 1,
  NSAN_SLOT_UNKNOWN = 2
};

enum {
  NSAN_BYTE_EMPTY = 0,
  NSAN_BYTE_OCCUPIED = 1,
  NSAN_BYTE_TOMBSTONE = 2
};

enum { NSAN_SHADOW_TABLE_SIZE = 1u << 20 };
static ShadowSlot ShadowTable[NSAN_SHADOW_TABLE_SIZE];
enum { NSAN_BYTE_TABLE_SIZE = 1u << 20 };
static ShadowByteSlot ShadowByteTable[NSAN_BYTE_TABLE_SIZE];
static atomic_flag ShadowTableLock = ATOMIC_FLAG_INIT;

typedef struct {
  uint64_t site;
  unsigned char occupied;
} ReportedSite;

enum { NSAN_REPORTED_TABLE_SIZE = 1u << 14 };
static ReportedSite ReportedSites[NSAN_REPORTED_TABLE_SIZE];
static atomic_flag ReportedSitesLock = ATOMIC_FLAG_INIT;
static atomic_flag ConfigLock = ATOMIC_FLAG_INIT;

static double RelErrorThreshold = -1.0;
static double AbsErrorThreshold = -1.0;
static double CancellationRatioThreshold = -1.0;
static int HaltOnError = -1;
static int ReportLoadDivergences = -1;
static int ReportCallDivergences = -1;
static int ReportFormatJson = -1;
static int DebugMemory = -1;

enum { NSAN_MAX_FLOAT_ARGS = 64 };
static _Thread_local double ArgShadows[NSAN_MAX_FLOAT_ARGS];
static _Thread_local unsigned char ArgShadowValid[NSAN_MAX_FLOAT_ARGS];
static _Thread_local double ReturnShadow;
static _Thread_local unsigned char ReturnShadowValid;

typedef struct {
  uintptr_t offset;
  double value;
  unsigned char state;
} ShadowTransfer;

static void store_shadow_addr_locked(const void *addr, double shadow,
                                     unsigned char state);
static void ensure_config(void);

static void nsan_lock(atomic_flag *lock) {
  while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {
  }
}

static void nsan_unlock(atomic_flag *lock) {
  atomic_flag_clear_explicit(lock, memory_order_release);
}

static uint64_t hash_ptr(const void *ptr) {
  uintptr_t x = (uintptr_t)ptr;
  x ^= x >> 33;
  x *= (uintptr_t)0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  return (uint64_t)x;
}

static bool ranges_overlap(uintptr_t a_begin, uintptr_t a_size,
                           uintptr_t b_begin, uintptr_t b_size) {
  uintptr_t a_end = a_begin + a_size;
  uintptr_t b_end = b_begin + b_size;
  return a_begin < b_end && b_begin < a_end;
}

static ShadowSlot *find_shadow_slot(const void *addr, ShadowSlot **insert_slot) {
  uint64_t start = hash_ptr(addr) & (NSAN_SHADOW_TABLE_SIZE - 1);
  ShadowSlot *first_unknown = NULL;
  for (uint64_t probe = 0; probe < NSAN_SHADOW_TABLE_SIZE; ++probe) {
    ShadowSlot *slot =
        &ShadowTable[(start + probe) & (NSAN_SHADOW_TABLE_SIZE - 1)];
    if (slot->state == NSAN_SLOT_EMPTY) {
      if (insert_slot)
        *insert_slot = first_unknown ? first_unknown : slot;
      return NULL;
    }
    if (slot->key == addr) {
      if (insert_slot)
        *insert_slot = slot;
      return slot;
    }
    if (slot->state == NSAN_SLOT_UNKNOWN && !first_unknown)
      first_unknown = slot;
  }
  if (insert_slot)
    *insert_slot = first_unknown;
  return NULL;
}

static ShadowByteSlot *find_byte_slot(const void *addr,
                                      ShadowByteSlot **insert_slot) {
  uint64_t start = hash_ptr(addr) & (NSAN_BYTE_TABLE_SIZE - 1);
  ShadowByteSlot *first_tombstone = NULL;
  for (uint64_t probe = 0; probe < NSAN_BYTE_TABLE_SIZE; ++probe) {
    ShadowByteSlot *slot =
        &ShadowByteTable[(start + probe) & (NSAN_BYTE_TABLE_SIZE - 1)];
    if (slot->state == NSAN_BYTE_EMPTY) {
      if (insert_slot)
        *insert_slot = first_tombstone ? first_tombstone : slot;
      return NULL;
    }
    if (slot->state == NSAN_BYTE_OCCUPIED && slot->byte_addr == addr) {
      if (insert_slot)
        *insert_slot = slot;
      return slot;
    }
    if (slot->state == NSAN_BYTE_TOMBSTONE && !first_tombstone)
      first_tombstone = slot;
  }
  if (insert_slot)
    *insert_slot = first_tombstone;
  return NULL;
}

static uint64_t hash_site(const char *file, unsigned line, unsigned column,
                          int op) {
  uint64_t h = 1469598103934665603ULL;
  const unsigned char *p = (const unsigned char *)(file ? file : "<unknown>");
  while (*p) {
    h ^= *p++;
    h *= 1099511628211ULL;
  }
  h ^= (uint64_t)line + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  h ^= (uint64_t)column + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  h ^= (uint64_t)op + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

static double read_env_double(const char *name, double fallback) {
  const char *value = getenv(name);
  if (!value || !*value)
    return fallback;
  char *end = NULL;
  double parsed = strtod(value, &end);
  return end && *end == '\0' ? parsed : fallback;
}

static int read_env_int(const char *name, int fallback) {
  const char *value = getenv(name);
  if (!value || !*value)
    return fallback;
  return atoi(value);
}

static void debug_memory(const char *fmt, ...) {
  if (DebugMemory < 0)
    ensure_config();
  if (DebugMemory != 1)
    return;
  va_list ap;
  va_start(ap, fmt);
  fputs("[nsan-debug] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

static void ensure_config(void) {
  if (RelErrorThreshold >= 0.0)
    return;
  nsan_lock(&ConfigLock);
  if (RelErrorThreshold >= 0.0) {
    nsan_unlock(&ConfigLock);
    return;
  }
  RelErrorThreshold = read_env_double("NSAN_REL_ERROR", 1e-5);
  AbsErrorThreshold = read_env_double("NSAN_ABS_ERROR", 1e-12);
  CancellationRatioThreshold =
      read_env_double("NSAN_CANCELLATION_RATIO", 1e6);
  HaltOnError = read_env_int("NSAN_HALT_ON_ERROR", 0);
  ReportLoadDivergences = read_env_int("NSAN_REPORT_LOADS", 0);
  ReportCallDivergences = read_env_int("NSAN_REPORT_CALLS", 0);
  DebugMemory = read_env_int("NSAN_DEBUG_MEMORY", 0);
  const char *format = getenv("NSAN_REPORT_FORMAT");
  ReportFormatJson = format && strcmp(format, "json") == 0;
  nsan_unlock(&ConfigLock);
}

static bool should_report_site(const char *file, unsigned line,
                               unsigned column, int op) {
  uint64_t site = hash_site(file, line, column, op);
  uint64_t start = site & (NSAN_REPORTED_TABLE_SIZE - 1);
  nsan_lock(&ReportedSitesLock);
  for (uint64_t probe = 0; probe < NSAN_REPORTED_TABLE_SIZE; ++probe) {
    ReportedSite *slot =
        &ReportedSites[(start + probe) & (NSAN_REPORTED_TABLE_SIZE - 1)];
    if (slot->occupied && slot->site == site) {
      nsan_unlock(&ReportedSitesLock);
      return false;
    }
    if (!slot->occupied) {
      slot->occupied = 1;
      slot->site = site;
      nsan_unlock(&ReportedSitesLock);
      return true;
    }
  }
  nsan_unlock(&ReportedSitesLock);
  return true;
}

static const char *op_name(int op) {
  switch (op) {
  case NSAN_OP_FADD:
    return "fadd";
  case NSAN_OP_FSUB:
    return "fsub";
  case NSAN_OP_FMUL:
    return "fmul";
  case NSAN_OP_FDIV:
    return "fdiv";
  case NSAN_OP_FREM:
    return "frem";
  case NSAN_OP_FNEG:
    return "fneg";
  case NSAN_OP_FPTRUNC:
    return "fptrunc";
  case NSAN_OP_SELECT:
    return "select";
  case NSAN_OP_LOAD:
    return "load";
  case NSAN_OP_CALL:
    return "call";
  default:
    return "unknown";
  }
}

static bool diverged(float actual, double shadow, double *abs_error_out,
                     double *rel_error_out) {
  double actual_as_double = (double)actual;
  double abs_error = fabs(actual_as_double - shadow);
  double scale = fmax(fabs(shadow), 1.0);
  double rel_error = abs_error / scale;
  if (abs_error_out)
    *abs_error_out = abs_error;
  if (rel_error_out)
    *rel_error_out = rel_error;

  if (isnan(actual_as_double) != isnan(shadow))
    return true;
  if (isinf(actual_as_double) != isinf(shadow))
    return true;
  return abs_error > AbsErrorThreshold && rel_error > RelErrorThreshold;
}

static const char *explanation_for(const char *kind, int op) {
  if (strcmp(kind, "catastrophic cancellation") == 0)
    return "subtraction/addition produced a result tiny relative to its inputs";
  if (strcmp(kind, "NaN/Inf propagation") == 0)
    return "float and shadow disagree on NaN or infinity state";
  if (op == NSAN_OP_CALL)
    return "function return differs from the propagated shadow return";
  return "float result diverges from the higher-precision shadow";
}

static void json_string(FILE *out, const char *text) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p;
       ++p) {
    switch (*p) {
    case '\\':
      fputs("\\\\", out);
      break;
    case '"':
      fputs("\\\"", out);
      break;
    case '\n':
      fputs("\\n", out);
      break;
    case '\r':
      fputs("\\r", out);
      break;
    case '\t':
      fputs("\\t", out);
      break;
    default:
      if (*p < 0x20)
        fprintf(out, "\\u%04x", *p);
      else
        fputc(*p, out);
      break;
    }
  }
  fputc('"', out);
}

static void report(const char *kind, float actual, double shadow,
                   double lhs_shadow, double rhs_shadow, int op,
                   const char *file, const char *function, unsigned line,
                   unsigned column, double abs_error, double rel_error,
                   double cancellation_ratio) {
  if (!should_report_site(file, line, column, op))
    return;

  const char *severity = strcmp(kind, "NaN/Inf propagation") == 0 ? "error"
                                                                  : "warning";
  const char *explanation = explanation_for(kind, op);

  if (ReportFormatJson) {
    fprintf(stderr, "{\"tool\":\"NumericalSanitizer\",\"severity\":");
    json_string(stderr, severity);
    fprintf(stderr, ",\"kind\":");
    json_string(stderr, kind);
    fprintf(stderr, ",\"function\":");
    json_string(stderr, function ? function : "<unknown>");
    fprintf(stderr, ",\"file\":");
    json_string(stderr, file ? file : "<unknown>");
    fprintf(stderr,
            ",\"line\":%u,\"column\":%u,\"op\":\"%s\","
            "\"actual\":%.9g,\"shadow\":%.17g,"
            "\"abs_error\":%.6e,\"rel_error\":%.6e,"
            "\"cancellation_ratio\":%.6e,"
            "\"lhs_shadow\":%.17g,\"rhs_shadow\":%.17g,\"explanation\":",
            line, column, op_name(op), actual, shadow, abs_error, rel_error,
            cancellation_ratio, lhs_shadow, rhs_shadow);
    json_string(stderr, explanation);
    fputs("}\n", stderr);
  } else {
    fprintf(stderr, "NumericalSanitizer %s: %s\n", severity, kind);
    fprintf(stderr, "  at %s:%u:%u in %s\n", file ? file : "<unknown>", line,
            column, function ? function : "<unknown>");
    fprintf(stderr, "  op=%s actual(float)=%.9g shadow(double)=%.17g\n",
            op_name(op), actual, shadow);
    fprintf(stderr, "  abs_error=%.6e rel_error=%.6e", abs_error, rel_error);
    if (!isnan(cancellation_ratio))
      fprintf(stderr, " cancellation_ratio=%.6e", cancellation_ratio);
    fputc('\n', stderr);
    if (!isnan(lhs_shadow) || !isnan(rhs_shadow))
      fprintf(stderr, "  lhs_shadow=%.17g rhs_shadow=%.17g\n", lhs_shadow,
              rhs_shadow);
    fprintf(stderr, "  note: %s\n", explanation);
  }

  if (HaltOnError)
    abort();
}

double NSAN_WEAK __nsan_shadow_from_float(float value) { return (double)value; }

void NSAN_WEAK __nsan_set_arg_shadow(unsigned index, double shadow) {
  if (index >= NSAN_MAX_FLOAT_ARGS)
    return;
  ArgShadows[index] = shadow;
  ArgShadowValid[index] = 1;
}

double NSAN_WEAK __nsan_get_arg_shadow(unsigned index, float actual) {
  if (index >= NSAN_MAX_FLOAT_ARGS || !ArgShadowValid[index])
    return (double)actual;
  ArgShadowValid[index] = 0;
  return ArgShadows[index];
}

void NSAN_WEAK __nsan_clear_arg_shadows(unsigned count) {
  if (count > NSAN_MAX_FLOAT_ARGS)
    count = NSAN_MAX_FLOAT_ARGS;
  for (unsigned i = 0; i < count; ++i)
    ArgShadowValid[i] = 0;
}

void NSAN_WEAK __nsan_set_return_shadow(double shadow) {
  ReturnShadow = shadow;
  ReturnShadowValid = 1;
}

double NSAN_WEAK __nsan_get_return_shadow(float actual) {
  if (!ReturnShadowValid)
    return (double)actual;
  ReturnShadowValid = 0;
  return ReturnShadow;
}

double NSAN_WEAK __nsan_shadow_sqrtf(float actual, double x) {
  (void)actual;
  return sqrt(x);
}

double NSAN_WEAK __nsan_shadow_sinf(float actual, double x) {
  (void)actual;
  return sin(x);
}

double NSAN_WEAK __nsan_shadow_cosf(float actual, double x) {
  (void)actual;
  return cos(x);
}

double NSAN_WEAK __nsan_shadow_tanf(float actual, double x) {
  (void)actual;
  return tan(x);
}

double NSAN_WEAK __nsan_shadow_expf(float actual, double x) {
  (void)actual;
  return exp(x);
}

double NSAN_WEAK __nsan_shadow_logf(float actual, double x) {
  (void)actual;
  return log(x);
}

double NSAN_WEAK __nsan_shadow_powf(float actual, double x, double y) {
  (void)actual;
  return pow(x, y);
}

double NSAN_WEAK __nsan_shadow_fabsf(float actual, double x) {
  (void)actual;
  return fabs(x);
}

double NSAN_WEAK __nsan_shadow_floorf(float actual, double x) {
  (void)actual;
  return floor(x);
}

double NSAN_WEAK __nsan_shadow_ceilf(float actual, double x) {
  (void)actual;
  return ceil(x);
}

double NSAN_WEAK __nsan_shadow_fmodf(float actual, double x, double y) {
  (void)actual;
  return fmod(x, y);
}

double NSAN_WEAK __nsan_shadow_atanf(float actual, double x) {
  (void)actual;
  return atan(x);
}

double NSAN_WEAK __nsan_shadow_atan2f(float actual, double x, double y) {
  (void)actual;
  return atan2(x, y);
}

double NSAN_WEAK __nsan_shadow_asinf(float actual, double x) {
  (void)actual;
  return asin(x);
}

double NSAN_WEAK __nsan_shadow_acosf(float actual, double x) {
  (void)actual;
  return acos(x);
}

double NSAN_WEAK __nsan_shadow_sinhf(float actual, double x) {
  (void)actual;
  return sinh(x);
}

double NSAN_WEAK __nsan_shadow_coshf(float actual, double x) {
  (void)actual;
  return cosh(x);
}

double NSAN_WEAK __nsan_shadow_load_float(const void *addr, float actual) {
  if (!addr)
    return (double)actual;

  nsan_lock(&ShadowTableLock);
  for (unsigned i = 0; i < sizeof(float); ++i) {
    const void *byte_addr = (const void *)((uintptr_t)addr + i);
    ShadowByteSlot *byte_slot = find_byte_slot(byte_addr, NULL);
    if (!byte_slot || byte_slot->base_addr != addr || byte_slot->offset != i) {
      nsan_unlock(&ShadowTableLock);
      debug_memory("load miss addr=%p byte=%p offset=%u actual=%g", addr,
                   byte_addr, i, (double)actual);
      return (double)actual;
    }
  }

  ShadowSlot *slot = find_shadow_slot(addr, NULL);
  if (!slot || slot->state != NSAN_SLOT_KNOWN) {
    nsan_unlock(&ShadowTableLock);
    debug_memory("load unknown addr=%p actual=%g", addr, (double)actual);
    return (double)actual;
  }
  double value = slot->value;
  nsan_unlock(&ShadowTableLock);
  debug_memory("load hit addr=%p shadow=%.17g actual=%g", addr, value,
               (double)actual);
  return value;
}

void NSAN_WEAK __nsan_shadow_store_float(const void *addr, double shadow) {
  if (!addr)
    return;

  nsan_lock(&ShadowTableLock);
  store_shadow_addr_locked(addr, shadow, NSAN_SLOT_KNOWN);
  nsan_unlock(&ShadowTableLock);
  debug_memory("store addr=%p shadow=%.17g", addr, shadow);
}

static void store_shadow_addr_locked(const void *addr, double shadow,
                                     unsigned char state) {
  ShadowSlot *insert_slot = NULL;
  ShadowSlot *slot = find_shadow_slot(addr, &insert_slot);
  if (slot) {
    slot->state = state;
    slot->value = shadow;
  } else if (insert_slot) {
    insert_slot->key = addr;
    insert_slot->value = shadow;
    insert_slot->state = state;
  }

  for (unsigned i = 0; i < sizeof(float); ++i) {
    const void *byte_addr = (const void *)((uintptr_t)addr + i);
    ShadowByteSlot *byte_insert = NULL;
    ShadowByteSlot *byte_slot = find_byte_slot(byte_addr, &byte_insert);
    if (byte_slot) {
      byte_slot->base_addr = addr;
      byte_slot->offset = (unsigned char)i;
      byte_slot->state = NSAN_BYTE_OCCUPIED;
    } else if (byte_insert) {
      byte_insert->byte_addr = byte_addr;
      byte_insert->base_addr = addr;
      byte_insert->offset = (unsigned char)i;
      byte_insert->state = NSAN_BYTE_OCCUPIED;
    }
  }
}

static void clear_byte_mappings_locked(uintptr_t begin, uintptr_t size) {
  for (uint64_t i = 0; i < NSAN_BYTE_TABLE_SIZE; ++i) {
    ShadowByteSlot *slot = &ShadowByteTable[i];
    if (slot->state != NSAN_BYTE_OCCUPIED || !slot->byte_addr)
      continue;
    uintptr_t byte = (uintptr_t)slot->byte_addr;
    if (byte >= begin && byte < begin + size) {
      slot->state = NSAN_BYTE_TOMBSTONE;
      slot->byte_addr = NULL;
      slot->base_addr = NULL;
      slot->offset = 0;
    }
  }
}

void NSAN_WEAK __nsan_forget_shadow_bytes(const void *addr, unsigned long size) {
  if (!addr || size == 0)
    return;

  uintptr_t begin = (uintptr_t)addr;
  nsan_lock(&ShadowTableLock);
  for (uint64_t i = 0; i < NSAN_SHADOW_TABLE_SIZE; ++i) {
    ShadowSlot *slot = &ShadowTable[i];
    if (slot->state == NSAN_SLOT_EMPTY || !slot->key)
      continue;
    uintptr_t slot_begin = (uintptr_t)slot->key;
    if (ranges_overlap(slot_begin, sizeof(float), begin, (uintptr_t)size))
      slot->state = NSAN_SLOT_UNKNOWN;
  }
  clear_byte_mappings_locked(begin, (uintptr_t)size);
  nsan_unlock(&ShadowTableLock);
  debug_memory("forget addr=%p size=%lu", addr, size);
}

static void transfer_shadow_bytes(const void *dst, const void *src,
                                  unsigned long size) {
  if (!dst || !src || size == 0)
    return;

  uintptr_t src_begin = (uintptr_t)src;
  uintptr_t dst_begin = (uintptr_t)dst;
  ShadowTransfer *transfers = NULL;
  size_t transfer_count = 0;
  size_t transfer_cap = 0;

  nsan_lock(&ShadowTableLock);
  for (uint64_t i = 0; i < NSAN_SHADOW_TABLE_SIZE; ++i) {
    ShadowSlot *slot = &ShadowTable[i];
    if (slot->state == NSAN_SLOT_EMPTY || !slot->key)
      continue;
    uintptr_t slot_begin = (uintptr_t)slot->key;
    if (slot_begin < src_begin || slot_begin + sizeof(float) > src_begin + size)
      continue;
    if (transfer_count == transfer_cap) {
      size_t new_cap = transfer_cap ? transfer_cap * 2 : 32;
      ShadowTransfer *new_transfers =
          (ShadowTransfer *)realloc(transfers, new_cap * sizeof(*transfers));
      if (!new_transfers) {
        free(transfers);
        nsan_unlock(&ShadowTableLock);
        return;
      }
      transfers = new_transfers;
      transfer_cap = new_cap;
    }
    transfers[transfer_count].offset = slot_begin - src_begin;
    transfers[transfer_count].value = slot->value;
    transfers[transfer_count].state = slot->state;
    debug_memory("queue transfer src=%p offset=%lu shadow=%.17g state=%u",
                 slot->key, (unsigned long)transfers[transfer_count].offset,
                 slot->value, (unsigned)slot->state);
    ++transfer_count;
  }

  for (uint64_t i = 0; i < NSAN_SHADOW_TABLE_SIZE; ++i) {
    ShadowSlot *slot = &ShadowTable[i];
    if (slot->state == NSAN_SLOT_EMPTY || !slot->key)
      continue;
    uintptr_t slot_begin = (uintptr_t)slot->key;
    if (ranges_overlap(slot_begin, sizeof(float), dst_begin, (uintptr_t)size))
      slot->state = NSAN_SLOT_UNKNOWN;
  }
  clear_byte_mappings_locked(dst_begin, (uintptr_t)size);

  for (size_t i = 0; i < transfer_count; ++i) {
    const void *dst_addr = (const void *)(dst_begin + transfers[i].offset);
    store_shadow_addr_locked(dst_addr, transfers[i].value, transfers[i].state);
    debug_memory("apply transfer dst=%p offset=%lu shadow=%.17g state=%u",
                 dst_addr, (unsigned long)transfers[i].offset,
                 transfers[i].value, (unsigned)transfers[i].state);
  }
  nsan_unlock(&ShadowTableLock);
  free(transfers);
  debug_memory("copy bytes dst=%p src=%p size=%lu transferred=%lu", dst, src,
               size, (unsigned long)transfer_count);
}

void NSAN_WEAK __nsan_copy_shadow_bytes(const void *dst, const void *src,
                                        unsigned long size) {
  transfer_shadow_bytes(dst, src, size);
}

void NSAN_WEAK __nsan_move_shadow_bytes(const void *dst, const void *src,
                                        unsigned long size) {
  transfer_shadow_bytes(dst, src, size);
}

void NSAN_WEAK __nsan_check_float(float actual, double shadow, int op,
                                  const char *file, const char *function,
                                  unsigned line, unsigned column) {
  ensure_config();
  double abs_error = 0.0;
  double rel_error = 0.0;
  if (!diverged(actual, shadow, &abs_error, &rel_error))
    return;

  bool nan_or_inf_mismatch =
      isnan((double)actual) != isnan(shadow) ||
      isinf((double)actual) != isinf(shadow);
  if (op == NSAN_OP_LOAD && !nan_or_inf_mismatch && !ReportLoadDivergences)
    return;
  if (op == NSAN_OP_CALL && !nan_or_inf_mismatch && !ReportCallDivergences)
    return;

  const char *kind = "float/shadow divergence";
  if (nan_or_inf_mismatch)
    kind = "NaN/Inf propagation";
  report(kind, actual, shadow, NAN, NAN, op, file, function, line, column,
         abs_error, rel_error, NAN);
}

void NSAN_WEAK __nsan_check_binary_float(float actual, double shadow,
                                         double lhs_shadow, double rhs_shadow,
                                         int op, const char *file,
                                         const char *function, unsigned line,
                                         unsigned column) {
  ensure_config();

  double abs_error = 0.0;
  double rel_error = 0.0;
  bool has_diverged = diverged(actual, shadow, &abs_error, &rel_error);
  double largest_input = fmax(fabs(lhs_shadow), fabs(rhs_shadow));
  double result_magnitude = fmax(fabs(shadow), DBL_MIN);
  double cancellation_ratio = largest_input / result_magnitude;

  if ((op == NSAN_OP_FADD || op == NSAN_OP_FSUB) &&
      cancellation_ratio >= CancellationRatioThreshold && has_diverged) {
    report("catastrophic cancellation", actual, shadow, lhs_shadow, rhs_shadow,
           op, file, function, line, column, abs_error, rel_error,
           cancellation_ratio);
    return;
  }

  if (!has_diverged)
    return;

  const char *kind = "float/shadow divergence";
  if (isnan((double)actual) != isnan(shadow) || isinf((double)actual) != isinf(shadow))
    kind = "NaN/Inf propagation";
  report(kind, actual, shadow, lhs_shadow, rhs_shadow, op, file, function, line,
         column, abs_error, rel_error, cancellation_ratio);
}
