// Loads the built plugin like the game does, with stub SDK callbacks, then
// checks the shared memory is created, filled and released.
// Usage: smoke_test <path to plugin library>
#include <cstdio>
#include <cstring>

#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_common_channels.h"
#include "common/scssdk_telemetry_common_configs.h"
#include "common/scssdk_telemetry_common_gameplay_events.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "scs-telemetry-common.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef SCSAPI_RESULT_FPTR(init_fn)(const scs_u32_t, const scs_telemetry_init_params_t* const);
typedef SCSAPI_VOID_FPTR(shutdown_fn)();

static SCSAPI_VOID stub_log(const scs_log_type_t, const scs_string_t message) {
  printf("[game log] %s\n", message);
}
// Callbacks the plugin registers, kept so the test can play the game's part.
static scs_telemetry_event_callback_t configuration_cb = nullptr;
static scs_telemetry_event_callback_t gameplay_cb = nullptr;
static scs_telemetry_channel_callback_t mandatory_break_cb = nullptr;
static scs_context_t mandatory_break_ctx = nullptr;

static SCSAPI_RESULT stub_register_event(const scs_event_t event, const scs_telemetry_event_callback_t callback,
                                         const scs_context_t) {
  if (event == SCS_TELEMETRY_EVENT_configuration) configuration_cb = callback;
  if (event == SCS_TELEMETRY_EVENT_gameplay) gameplay_cb = callback;
  return SCS_RESULT_ok;
}
static SCSAPI_RESULT stub_unregister_event(const scs_event_t) { return SCS_RESULT_ok; }
static SCSAPI_RESULT stub_register_channel(const scs_string_t name, const scs_u32_t, const scs_value_type_t,
                                           const scs_u32_t, const scs_telemetry_channel_callback_t callback,
                                           const scs_context_t context) {
  if (strcmp(name, SCS_TELEMETRY_CHANNEL_next_mandatory_break) == 0) {
    mandatory_break_cb = callback;
    mandatory_break_ctx = context;
  }
  return SCS_RESULT_ok;
}

static scs_named_value_t attr_string(const char* name, const char* value) {
  scs_named_value_t v;
  memset(&v, 0, sizeof v);
  v.name = name;
  v.index = SCS_U32_NIL;
  v.value.type = SCS_VALUE_TYPE_string;
  v.value.value_string.value = value;
  return v;
}
static scs_named_value_t attr_u32(const char* name, scs_u32_t value) {
  scs_named_value_t v;
  memset(&v, 0, sizeof v);
  v.name = name;
  v.index = SCS_U32_NIL;
  v.value.type = SCS_VALUE_TYPE_u32;
  v.value.value_u32.value = value;
  return v;
}
static scs_named_value_t attr_u64(const char* name, scs_u64_t value) {
  scs_named_value_t v = attr_u32(name, 0);
  v.value.type = SCS_VALUE_TYPE_u64;
  v.value.value_u64.value = value;
  return v;
}
static scs_named_value_t attr_s64(const char* name, scs_s64_t value) {
  scs_named_value_t v = attr_u32(name, 0);
  v.value.type = SCS_VALUE_TYPE_s64;
  v.value.value_s64.value = value;
  return v;
}
static scs_named_value_t attr_s32(const char* name, scs_s32_t value) {
  scs_named_value_t v = attr_u32(name, 0);
  v.value.type = SCS_VALUE_TYPE_s32;
  v.value.value_s32.value = value;
  return v;
}
static scs_named_value_t attr_float(const char* name, float value) {
  scs_named_value_t v = attr_u32(name, 0);
  v.value.type = SCS_VALUE_TYPE_float;
  v.value.value_float.value = value;
  return v;
}
static scs_named_value_t attr_bool(const char* name, bool value) {
  scs_named_value_t v = attr_u32(name, 0);
  v.value.type = SCS_VALUE_TYPE_bool;
  v.value.value_bool.value = value ? 1 : 0;
  return v;
}
static scs_named_value_t attr_end() {
  scs_named_value_t v;
  memset(&v, 0, sizeof v);
  return v;
}

static void send_config(const char* id, const scs_named_value_t* attributes) {
  scs_telemetry_configuration_t info;
  info.id = id;
  info.attributes = attributes;
  configuration_cb(SCS_TELEMETRY_EVENT_configuration, &info, nullptr);
}
static void send_gameplay(const char* id, const scs_named_value_t* attributes) {
  scs_telemetry_gameplay_event_t info;
  info.id = id;
  info.attributes = attributes;
  gameplay_cb(SCS_TELEMETRY_EVENT_gameplay, &info, nullptr);
}
static SCSAPI_RESULT stub_unregister_channel(const scs_string_t, const scs_u32_t, const scs_value_type_t) {
  return SCS_RESULT_ok;
}

static int fail(const char* what) {
  fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

int main(int argc, char** argv) {
  if (argc < 2) return fail("usage: smoke_test <plugin>");

#ifdef _WIN32
  HMODULE lib = LoadLibraryA(argv[1]);
  if (!lib) return fail("cannot load plugin");
  auto init = reinterpret_cast<init_fn>(GetProcAddress(lib, "scs_telemetry_init"));
  auto shutdown = reinterpret_cast<shutdown_fn>(GetProcAddress(lib, "scs_telemetry_shutdown"));
#else
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) return fail(dlerror());
  auto init = reinterpret_cast<init_fn>(dlsym(lib, "scs_telemetry_init"));
  auto shutdown = reinterpret_cast<shutdown_fn>(dlsym(lib, "scs_telemetry_shutdown"));
#endif
  if (!init || !shutdown) return fail("entry points not exported");

  scs_telemetry_init_params_v101_t params;
  memset(&params, 0, sizeof params);
  params.common.game_name = "Euro Truck Simulator 2";
  params.common.game_id = SCS_GAME_ID_EUT2;
  params.common.game_version = SCS_TELEMETRY_EUT2_GAME_VERSION_CURRENT;
  params.common.log = stub_log;
  params.register_for_event = stub_register_event;
  params.unregister_from_event = stub_unregister_event;
  params.register_for_channel = stub_register_channel;
  params.unregister_from_channel = stub_unregister_channel;

  if (init(SCS_TELEMETRY_VERSION_1_01, &params) != SCS_RESULT_ok) return fail("scs_telemetry_init failed");

  // Read the memory from the outside, like the app does.
#ifdef _WIN32
  HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, SCS_PLUGIN_MMF_NAME);
  if (!map) return fail("shared memory not found");
  auto view = static_cast<const scsTelemetryMap_t*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, SCS_PLUGIN_MMF_SIZE));
#else
  int fd = shm_open(SCS_PLUGIN_MMF_NAME, O_RDONLY, 0);
  if (fd == -1) return fail("shared memory not found");
  auto view = static_cast<const scsTelemetryMap_t*>(mmap(nullptr, SCS_PLUGIN_MMF_SIZE, PROT_READ, MAP_SHARED, fd, 0));
  if (view == MAP_FAILED) view = nullptr;
#endif
  if (!view) return fail("cannot map shared memory");

  printf("sdkActive=%d revision=%u game=%u telemetry=%u.%u\n", view->sdkActive,
         view->scs_values.telemetry_plugin_revision, view->scs_values.game,
         view->scs_values.version_major, view->scs_values.version_minor);
  if (!view->sdkActive) return fail("sdkActive not set");
  if (view->scs_values.telemetry_plugin_revision != PLUGIN_REVID) return fail("wrong plugin revision");
  if (view->scs_values.game != ETS2) return fail("game not detected as ETS2");

  // --- SDK 1.15 data (revision 13 zone) ---
  if (!configuration_cb || !gameplay_cb) return fail("configuration/gameplay events not registered");
  if (!mandatory_break_cb) return fail("mandatory break channel not registered");

  scs_value_t brk;
  memset(&brk, 0, sizeof brk);
  brk.type = SCS_VALUE_TYPE_s32;
  brk.value_s32.value = 431;
  mandatory_break_cb(SCS_TELEMETRY_CHANNEL_next_mandatory_break, SCS_U32_NIL, &brk, mandatory_break_ctx);
  if (view->sdk115.nextMandatoryBreak != 431) return fail("mandatory break not stored");

  // A truck job is running; car job events must not touch it.
  const scs_named_value_t truck_job[] = {
      attr_string(SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_id, "planks"), attr_end()};
  send_config(SCS_TELEMETRY_CONFIG_job, truck_job);

  const scs_named_value_t car_job[] = {
      attr_string(SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_id, "sedan"),
      attr_string(SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city, "Reno"),
      attr_string(SCS_TELEMETRY_CONFIG_ATTRIBUTE_car_job_market, "quick_job"),
      attr_u64(SCS_TELEMETRY_CONFIG_ATTRIBUTE_income, 12345),
      attr_u32(SCS_TELEMETRY_CONFIG_ATTRIBUTE_planned_distance_km, 321),
      attr_bool(SCS_TELEMETRY_CONFIG_ATTRIBUTE_customer_prio_time, true),
      attr_end()};
  send_config(SCS_TELEMETRY_CONFIG_car_job, car_job);
  if (!view->carJob.onJob) return fail("car job not started");
  if (strcmp(view->carJob.cargoId, "sedan") != 0 || strcmp(view->carJob.cityDst, "Reno") != 0 ||
      strcmp(view->carJob.jobMarket, "quick_job") != 0)
    return fail("car job strings wrong");
  if (view->carJob.income != 12345 || view->carJob.plannedDistanceKm != 321 || !view->carJob.customerPrioTime)
    return fail("car job values wrong");

  const scs_named_value_t delivered[] = {
      attr_s64(SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_revenue, 9999),
      attr_s32(SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_earned_xp, 250),
      attr_float(SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_vehicle_damage, 0.25f),
      attr_u32(SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_delivery_time, 600),
      attr_end()};
  const bool truck_delivered_before = view->special_b.jobDelivered;
  send_gameplay(SCS_TELEMETRY_GAMEPLAY_EVENT_car_job_delivered, delivered);
  if (!view->carJob.jobDelivered || view->carJob.onJob) return fail("car job delivery not flagged");
  if (view->carJob.deliveredRevenue != 9999 || view->carJob.deliveredEarnedXp != 250 ||
      view->carJob.deliveredVehicleDamage != 0.25f || view->carJob.deliveredDeliveryTime != 600)
    return fail("car job delivery values wrong");
  if (view->special_b.jobDelivered != truck_delivered_before) return fail("car job delivery flagged a truck delivery");
  if (strcmp(view->config_s.cargoId, "planks") != 0) return fail("car job delivery wiped the truck job");

  // Unknown events must not be handled as a cancelled truck job.
  const scs_named_value_t unknown[] = {attr_s64(SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_cancel_penalty, 1), attr_end()};
  send_gameplay("future.event", unknown);
  if (strcmp(view->config_s.cargoId, "planks") != 0) return fail("unknown event wiped the truck job");

  const scs_named_value_t bus_job[] = {attr_string("whatever", "x"), attr_end()};
  send_config(SCS_TELEMETRY_CONFIG_bus_job, bus_job);
  if (!view->sdk115.onBusJob) return fail("bus job not flagged");
  const scs_named_value_t no_bus_job[] = {attr_end()};
  send_config(SCS_TELEMETRY_CONFIG_bus_job, no_bus_job);
  if (view->sdk115.onBusJob) return fail("bus job not cleared");

#ifdef _WIN32
  UnmapViewOfFile(view);
  CloseHandle(map);
#else
  munmap(const_cast<scsTelemetryMap_t*>(view), SCS_PLUGIN_MMF_SIZE);
  close(fd);
#endif

  shutdown();

#ifndef _WIN32
  // The name must be gone so readers see the game as disconnected.
  int after = shm_open(SCS_PLUGIN_MMF_NAME, O_RDONLY, 0);
  if (after != -1) {
    close(after);
    return fail("shared memory still present after shutdown");
  }
#endif

  printf("OK\n");
  return 0;
}
