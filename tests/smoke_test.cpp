// Loads the built plugin like the game does, with stub SDK callbacks, then
// checks the shared memory is created, filled and released.
// Usage: smoke_test <path to plugin library>
#include <cstdio>
#include <cstring>

#include "scssdk_telemetry.h"
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
static SCSAPI_RESULT stub_register_event(const scs_event_t, const scs_telemetry_event_callback_t, const scs_context_t) {
  return SCS_RESULT_ok;
}
static SCSAPI_RESULT stub_unregister_event(const scs_event_t) { return SCS_RESULT_ok; }
static SCSAPI_RESULT stub_register_channel(const scs_string_t, const scs_u32_t, const scs_value_type_t, const scs_u32_t,
                                           const scs_telemetry_channel_callback_t, const scs_context_t) {
  return SCS_RESULT_ok;
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
