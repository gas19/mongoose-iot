/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#include <stdio.h>

#include "common/platforms/esp8266/esp_missing_includes.h"
#include "common/platforms/esp8266/esp_uart.h"

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"
#include "mem.h"
#include "fw/platforms/esp8266/user/v7_esp.h"
#include "fw/platforms/esp8266/user/util.h"
#include "fw/platforms/esp8266/user/esp_exc.h"

#include "fw/src/device_config.h"
#include "fw/src/sj_app.h"
#include "fw/src/sj_common.h"
#include "fw/src/sj_mongoose.h"
#include "fw/src/sj_prompt.h"
#include "fw/src/sj_hal.h"
#include "fw/src/sj_v7_ext.h"
#include "fw/src/sj_gpio_js.h"
#include "fw/src/sj_updater_clubby.h"
#include "fw/src/sj_updater_post.h"

#include "fw/platforms/esp8266/user/esp_fs.h"
#include "fw/platforms/esp8266/user/esp_sj_uart.h"
#include "fw/platforms/esp8266/user/esp_sj_uart_js.h"
#include "fw/platforms/esp8266/user/esp_updater.h"
#include "mongoose/mongoose.h" /* For cs_log_set_level() */
#include "common/platforms/esp8266/esp_umm_malloc.h"

#ifndef CS_DISABLE_JS
#include "v7/v7.h"
#endif

os_timer_t startcmd_timer;

#ifndef ESP_DEBUG_UART
#define ESP_DEBUG_UART 1
#endif
#ifndef ESP_DEBUG_UART_BAUD_RATE
#define ESP_DEBUG_UART_BAUD_RATE 115200
#endif

#ifdef ESP_ENABLE_HEAP_LOG
/*
 * global flag that is needed for heap trace: we shouldn't send anything to
 * uart until it is initialized
 */
int uart_initialized = 0;
#endif

void dbg_putc(char c) {
  fputc(c, stderr);
}

/*
 * Mongoose IoT initialization, called as an SDK timer callback
 * (`os_timer_...()`).
 */
int sjs_init(rboot_config *bcfg) {
  mongoose_init();
  /*
   * In order to see debug output (at least errors) during boot we have to
   * initialize debug in this point. But default we put debug to UART0 with
   * level=LL_ERROR, then configuration is loaded this settings are overridden
   */
  {
    struct esp_uart_config *u0cfg = esp_sj_uart_default_config(0);
#if ESP_DEBUG_UART == 0
    u0cfg->baud_rate = ESP_DEBUG_UART_BAUD_RATE;
#endif
    esp_uart_init(u0cfg);
    struct esp_uart_config *u1cfg = esp_sj_uart_default_config(1);
    /* UART1 has no RX pin, no point in allocating a buffer. */
    u1cfg->rx_buf_size = 0;
#if ESP_DEBUG_UART == 1
    u1cfg->baud_rate = ESP_DEBUG_UART_BAUD_RATE;
#endif
    esp_uart_init(u1cfg);
    fs_set_stdout_uart(0);
    fs_set_stderr_uart(ESP_DEBUG_UART);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    cs_log_set_level(LL_DEBUG);
    os_install_putc1(dbg_putc);
    system_set_os_print(1);
#ifdef ESP_ENABLE_HEAP_LOG
    uart_initialized = 1;
#endif
  }

  int r = fs_init(bcfg->fs_addresses[bcfg->current_rom],
                  bcfg->fs_sizes[bcfg->current_rom]);
  if (r != 0) {
    LOG(LL_ERROR, ("FS init error: %d", r));
    return -1;
  }
  if (bcfg->fw_updated && apply_update(bcfg) < 0) {
    return -2;
  }

#ifndef CS_DISABLE_JS
  init_v7(&bcfg);

  /* Disable GC during JS API initialization. */
  v7_set_gc_enabled(v7, 0);
#endif

  esp_sj_uart_init(v7);

  sj_common_api_setup(v7);
  sj_common_init(v7);

  sj_init_sys(v7);

  /* NOTE(lsm): must be done after mongoose_init(). */
  if (!init_device(v7)) {
    LOG(LL_ERROR, ("init_device failed"));
    return -3;
  }

  esp_print_reset_info();

#ifndef DISABLE_OTA
  sj_updater_post_init(v7);
  init_updater_clubby(v7);
#endif
  LOG(LL_INFO, ("Sys init done, SDK %s", system_get_sdk_version()));

  if (!sj_app_init(v7)) {
    LOG(LL_ERROR, ("App init failed"));
    return -4;
  }
  LOG(LL_INFO, ("App init done"));

#ifndef CS_DISABLE_JS
  /* SJS initialized, enable GC back, and trigger it. */
  v7_set_gc_enabled(v7, 1);
  v7_gc(v7, 1);
#endif

#if !defined(V7_NO_FS) && !defined(CS_DISABLE_JS)
  run_init_script();
#endif

#ifndef CS_DISABLE_JS
  /* Install prompt if enabled in the config and user's app has not installed
   * a custom RX handler. */
  if (get_cfg()->debug.enable_prompt &&
      v7_is_undefined(esp_sj_uart_get_recv_handler(0))) {
    sj_prompt_init(v7);
    esp_sj_uart_set_prompt(0);
  }
#endif

#ifdef ESP_UMM_ENABLE
  /*
   * We want to use our own heap functions instead of the ones provided by the
   * SDK.
   *
   * We have marked `pvPortMalloc` and friends weak, so that we can override
   * them with our own implementations, but to actually make it work, we have
   * to reference any function from the file with our implementation, so that
   * linker will not garbage-collect the whole compilation unit.
   *
   * So, we have a call to the no-op `esp_umm_init()` here.
   */
  esp_umm_init();
#endif

  sj_wdt_set_timeout(get_cfg()->sys.wdt_timeout);
  return 0;
}

void sjs_init_timer_cb(void *arg) {
  rboot_config *bcfg = get_rboot_config();
  if (sjs_init(bcfg) == 0) {
    if (bcfg->is_first_boot) {
      /* fw_updated will be reset by the boot loader if it's a rollback. */
      clubby_updater_finish(bcfg->fw_updated ? 0 : -1);
      commit_update(bcfg);
    }
  } else {
    if (bcfg->fw_updated) revert_update(bcfg);
    sj_system_restart(0);
  }
  (void) arg;
}

/*
 * Called when SDK initialization is finished
 */
void sdk_init_done_cb() {
  srand(system_get_rtc_time());

#if !defined(ESP_ENABLE_HW_WATCHDOG)
  ets_wdt_disable();
#endif
  system_soft_wdt_stop(); /* give 60 sec for initialization */

  /* Schedule SJS initialization (`sjs_init()`) */
  os_timer_disarm(&startcmd_timer);
  os_timer_setfn(&startcmd_timer, sjs_init_timer_cb, NULL);
  os_timer_arm(&startcmd_timer, 0, 0);
}

/* Init function */
void user_init() {
  system_update_cpu_freq(SYS_CPU_160MHZ);
  system_init_done_cb(sdk_init_done_cb);

  uart_div_modify(ESP_DEBUG_UART, UART_CLK_FREQ / 115200);

  esp_exception_handler_init();

  gpio_init();
}
