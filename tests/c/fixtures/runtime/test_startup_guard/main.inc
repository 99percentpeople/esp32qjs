static esp32qjs_runtime_t boot(esp_reset_reason_t reason)
{
    esp32qjs_runtime_t runtime = {0};
    runtime.config.startup_guard = true;
    runtime.config.startup_failure_limit = 2;
    runtime.config.startup_healthy_ms = 30000;
    runtime.config.autorun_startup_script = true;
    runtime.littlefs_mounted = true;
    runtime.ctx = (void *)1;
    strcpy(runtime.startup_script, "index.js");
    reset_reason = reason;
    now_us = 1;
    assert(runtime_boot_guard_load(&runtime));
    return runtime;
}

int main(void)
{
    esp32qjs_runtime_t runtime = boot(ESP_RST_POWERON);
#if TEST_CASE == 1
    for (unsigned failure = 1; failure <= 6; failure++) {
        runtime_boot_guard_arm(&runtime);
        runtime_boot_guard_fail(&runtime, "startup-exception");
        assert(persisted.failures == failure - 1 && persisted.failed);
        runtime = boot(ESP_RST_SW);
        unsigned level = failure / 2;
        if (level > 2) level = 2;
        assert(runtime.safe_mode == level);
        assert(runtime.startup_failure_count == failure && !persisted.failed);
        assert(!strcmp(runtime.last_startup_failure_reason, "startup-exception"));
    }
#elif TEST_CASE == 2
    esp_reset_reason_t external[] = {ESP_RST_SW, ESP_RST_USB, ESP_RST_POWERON, ESP_RST_EXT};
    for (unsigned level = 1; level <= 2; level++) {
        for (unsigned i = 0; i < sizeof(external) / sizeof(external[0]); i++) {
            persisted.failures = level * 2 - 1;
            runtime = boot(ESP_RST_TASK_WDT);
            assert(runtime.safe_mode == level);
            if (external[i] != ESP_RST_SW) {
                // A physical reset wins even if firmware was about to reboot on an exception.
                runtime_boot_guard_fail(&runtime, "startup-exception");
            } else {
                runtime_boot_guard_disarm_intentional(&runtime);
            }
            runtime = boot(external[i]);
            assert(runtime.safe_mode == 0 && runtime.startup_failure_count == 0);
            assert(!persisted.failures && !persisted.failed && !persisted.reason[0]);
        }
    }
#elif TEST_CASE == 3
    runtime = boot(ESP_RST_TASK_WDT);
    runtime = boot(ESP_RST_PANIC);
    assert(runtime.safe_mode == 1);
    runtime_boot_guard_arm(&runtime);
    runtime_boot_guard_note_returned(&runtime);
    now_us += 29999000;
    runtime_boot_guard_poll_healthy(&runtime);
    assert(runtime.startup_stabilizing);
    now_us += 1000;
    runtime_boot_guard_poll_healthy(&runtime);
    assert(!runtime.startup_pending && !runtime.startup_stabilizing);
    assert(runtime.safe_mode == 1 && persisted.failures == 2);
    runtime = boot(ESP_RST_TASK_WDT);
    assert(runtime.safe_mode == 1 && runtime.startup_failure_count == 3);
    runtime = boot(ESP_RST_TASK_WDT);
    assert(runtime.safe_mode == 2);
#elif TEST_CASE == 4
    runtime_boot_guard_fail(&runtime, "secondary-filesystem");
    runtime = boot(ESP_RST_SW);
    assert(runtime.safe_mode == 0 && runtime.startup_failure_count == 1);
    assert(!strcmp(runtime.last_startup_failure_reason, "secondary-filesystem"));
    runtime_boot_guard_fail(&runtime, "startup-exception");
    // A watchdog racing the planned software reboot is one physical reset.
    runtime = boot(ESP_RST_TASK_WDT);
    assert(runtime.safe_mode == 1 && runtime.startup_failure_count == 2);
    assert(!strcmp(runtime.last_startup_failure_reason, "task-watchdog"));
    assert(!persisted.failed);
    runtime = boot(ESP_RST_SW);
    assert(runtime.safe_mode == 0 && runtime.startup_failure_count == 0);
#elif TEST_CASE == 5
    runtime_run_startup(&runtime);
    assert(startup_loads == 1);
    runtime = boot(ESP_RST_PANIC);
    runtime = boot(ESP_RST_PANIC);
    runtime_run_startup(&runtime);
    assert(runtime.safe_mode == 1 && startup_loads == 2);
    runtime = boot(ESP_RST_TASK_WDT);
    runtime = boot(ESP_RST_TASK_WDT);
    runtime_run_startup(&runtime);
    assert(runtime.safe_mode == 2 && startup_loads == 2);
    assert(!runtime.startup_pending && !runtime.startup_stabilizing);
    runtime = boot(ESP_RST_EXT);
    runtime_run_startup(&runtime);
    assert(runtime.safe_mode == 0 && startup_loads == 3);
#endif
    return 0;
}
