/* Included by the Mesh JS adapter. Native jobs never retain JS values. */
static void mesh_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (!state) return;
    if (state->operation == MESH_SCAN_NEXT)
        esp32_mquickjs_wifi_mesh_scan_read_release(state->session, state->read_identity);
    else esp32_mquickjs_wifi_mesh_read_release(state->session, state->to_ds, state->read_identity);
    esp32_mquickjs_wifi_mesh_job_release(state->job);
    if (state->bytes) {
        esp32_mquickjs_wireless_secure_zero(state->bytes, MESH_MTU);
        esp32_mquickjs_memory_payload_free(state->bytes);
    }
    esp32_mquickjs_wifi_mesh_session_release(state->session);
    esp32_mquickjs_wireless_secure_zero(state, sizeof(*state));
    esp32_mquickjs_memory_payload_free(state);
}

static bool mesh_command_options(JSContext *ctx, JSGCRef *input,
    esp32_mquickjs_future_driver_state_t *state)
{
    static const char *const send_keys[] = {"data", "destination", "address", "ipv4", "port", "protocol",
        "reliable", "dropOnRootChange", "timeoutMs"};
    static const char *const read_keys[] = {"toDS", "timeoutMs"};
    static const char *const group_keys[] = {"addresses", "timeoutMs"};
    static const char *const tods_keys[] = {"reachable", "timeoutMs"};
    static const char *const wait_keys[] = {"timeoutMs"};
    mesh_operation_t op = state->operation;
    const char *const *keys = wait_keys; size_t key_count = 1;
    if (op == MESH_SEND) { keys = send_keys; key_count = sizeof(send_keys)/sizeof(*send_keys); }
    else if (op == MESH_RECEIVE) { keys = read_keys; key_count = 2; }
    else if (op == MESH_ADD_GROUPS || op == MESH_REMOVE_GROUPS) { keys = group_keys; key_count = 2; }
    else if (op == MESH_SET_TODS) { keys = tods_keys; key_count = 2; }
    bool required = op == MESH_SEND || op == MESH_ADD_GROUPS || op == MESH_REMOVE_GROUPS || op == MESH_SET_TODS;
    if (JS_IsUndefined(input->val) && !required) return true;
    if (!esp32_mquickjs_validate_plain_options(ctx, input->val, mesh_methods[op], keys, key_count)) return false;
    JSGCRef field_ref, array_ref;
    JSValue *field = JS_PushGCRef(ctx, &field_ref), *array = JS_PushGCRef(ctx, &array_ref);
    bool ok = false; size_t choice; uint16_t length; uint32_t number;
    esp32_mquickjs_wifi_mesh_send_t send = {.reliable = true, .protocol = MESH_PROTO_BIN};
    esp32_mquickjs_wifi_mesh_control_t control = {0};
    mesh_addr_t *addresses = NULL;
#define FIELD(key) do { *field = JS_GetPropertyStr(ctx, input->val, key); if (JS_IsException(*field)) goto done; } while (0)
    FIELD("timeoutMs");
    if (!JS_IsUndefined(*field) && !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1,
        ESP32_MQUICKJS_MESH_MAX_WAIT_MS, &state->timeout_ms)) goto invalid;
    if (op == MESH_RECEIVE) {
        FIELD("toDS"); if (!JS_IsUndefined(*field)) {
            if (!JS_IsBool(*field)) goto invalid;
            state->to_ds = *field == JS_TRUE;
        }
    } else if (op == MESH_SEND) {
        FIELD("destination"); if (!JS_IsUndefined(*field)) {
            static const char *const values[] = {"root", "peer", "toDS", "fromDS", "group", "broadcast"};
            if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 6, &choice)) goto invalid;
            send.destination = choice;
        }
        FIELD("protocol"); if (!JS_IsUndefined(*field)) {
            static const char *const values[] = {"binary", "http", "json", "mqtt"};
            if (!esp32_mquickjs_value_to_enum(ctx, *field, values, 4, &choice)) goto invalid;
            send.protocol = choice;
        }
        FIELD("reliable"); if (!JS_IsUndefined(*field)) {
            if (!JS_IsBool(*field)) goto invalid;
            send.reliable = *field == JS_TRUE;
        }
        FIELD("dropOnRootChange"); if (!JS_IsUndefined(*field)) {
            if (!JS_IsBool(*field) || send.destination != ESP32_MQUICKJS_MESH_TO_DS) goto invalid;
            send.drop_on_root_change = *field == JS_TRUE;
        }
        if (send.destination == ESP32_MQUICKJS_MESH_TO_ROOT && !send.reliable) goto invalid;
        bool mac = send.destination == ESP32_MQUICKJS_MESH_TO_PEER ||
            send.destination == ESP32_MQUICKJS_MESH_FROM_DS || send.destination == ESP32_MQUICKJS_MESH_TO_GROUP;
        FIELD("address");
        if (mac) {
            if (!mesh_bytes(ctx, *field, send.address.addr, 6, &length) || length != 6) goto invalid;
            uint8_t nonzero = 0; for (unsigned i = 0; i < 6; ++i) nonzero |= send.address.addr[i];
            if (!nonzero) goto invalid;
        } else if (!JS_IsUndefined(*field)) goto invalid;
        FIELD("ipv4");
        if (send.destination == ESP32_MQUICKJS_MESH_TO_DS) {
            if (!mesh_bytes(ctx, *field, (uint8_t *)&send.address.mip.ip4.addr, 4, &length) ||
                length != 4 || !send.address.mip.ip4.addr) goto invalid;
        } else if (!JS_IsUndefined(*field)) goto invalid;
        FIELD("port");
        if (send.destination == ESP32_MQUICKJS_MESH_TO_DS) {
            if (!esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1, UINT16_MAX, &number)) goto invalid;
            send.address.mip.port = number;
        } else if (!JS_IsUndefined(*field)) goto invalid;
        state->bytes = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, MESH_MTU,
            ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
        if (!state->bytes) { JS_ThrowOutOfMemory(ctx); goto done; }
        FIELD("data");
        if (!mesh_bytes(ctx, *field, state->bytes, MESH_MPS, &length) || !length) goto invalid;
        send.bytes = state->bytes; send.length = length;
        state->error = esp32_mquickjs_wifi_mesh_job_create(state->session, &send, NULL, state->timeout_ms, &state->job);
        esp32_mquickjs_wireless_secure_zero(state->bytes, MESH_MTU);
        esp32_mquickjs_memory_payload_free(state->bytes); state->bytes = NULL;
        if (state->error) goto native_error;
    } else if (op == MESH_ADD_GROUPS || op == MESH_REMOVE_GROUPS) {
        FIELD("addresses"); *array = *field;
        if (!JS_IsArray(ctx, *array)) goto invalid;
        *field = JS_GetPropertyStr(ctx, *array, "length");
        if (JS_IsException(*field) || !esp32_mquickjs_value_to_bounded_u32(ctx, *field, 1,
            ESP32_MQUICKJS_MESH_MAX_GROUPS, &number)) goto invalid;
        control.capacity = control.count = number;
        control.kind = op == MESH_ADD_GROUPS ? ESP32_MQUICKJS_MESH_GROUP_ADD : ESP32_MQUICKJS_MESH_GROUP_DELETE;
        addresses = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", number, sizeof(*addresses),
            ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (!addresses) { JS_ThrowOutOfMemory(ctx); goto done; }
        control.addresses = addresses;
        for (unsigned i = 0; i < control.count; ++i) {
            *field = JS_GetPropertyUint32(ctx, *array, i);
            if (JS_IsException(*field) || !mesh_bytes(ctx, *field, addresses[i].addr, 6, &length) || length != 6) goto invalid;
            uint8_t nonzero = 0; for (unsigned j = 0; j < 6; ++j) nonzero |= addresses[i].addr[j];
            if (!nonzero) goto invalid;
            for (unsigned j = 0; j < i; ++j) if (!memcmp(addresses[j].addr, addresses[i].addr, 6)) goto invalid;
        }
        state->error = esp32_mquickjs_wifi_mesh_job_create(state->session, NULL, &control, state->timeout_ms, &state->job);
        if (state->error) goto native_error;
    } else if (op == MESH_SET_TODS) {
        FIELD("reachable"); if (!JS_IsBool(*field)) goto invalid;
        control.kind = ESP32_MQUICKJS_MESH_TODS_STATE; control.value = *field == JS_TRUE;
        state->error = esp32_mquickjs_wifi_mesh_job_create(state->session, NULL, &control, state->timeout_ms, &state->job);
        if (state->error) goto native_error;
    }
    ok = true; goto done;
native_error:
    mesh_error(ctx, mesh_methods[op], state->session, state->error, false); goto done;
invalid:
    if (!JS_HasException(ctx)) JS_ThrowTypeError(ctx, "invalid Mesh command options");
done:
    esp32_mquickjs_memory_payload_free(addresses);
    JS_PopGCRef(ctx, &array_ref); JS_PopGCRef(ctx, &field_ref); return ok;
#undef FIELD
}

static bool mesh_future_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out, mesh_operation_t op)
{
    *out = NULL;
    if (argc > 1) { JS_ThrowTypeError(ctx, "Mesh method expects one options object"); return false; }
    esp32_mquickjs_wifi_mesh_session_t *session = mesh_receiver(ctx, self->val);
    if (!session) return false;
    if (!esp32_mquickjs_wifi_mesh_session_retain(session)) {
        JS_ThrowInternalError(ctx, "Mesh reference exhausted"); return false;
    }
    esp32_mquickjs_future_driver_state_t *state = esp32_mquickjs_memory_wireless_calloc("wireless.future", 1, sizeof(*state),
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
    if (!state) { esp32_mquickjs_wifi_mesh_session_release(session); JS_ThrowOutOfMemory(ctx); return false; }
    state->session = session; state->operation = op; state->timeout_ms = 30000;
    JSGCRef input_ref; JSValue *input = JS_PushGCRef(ctx, &input_ref);
    *input = argc ? argv[0].val : JS_UNDEFINED;
    bool valid = op >= MESH_CONFIGURATION ? mesh_control_options(ctx, &input_ref, state) : mesh_command_options(ctx, &input_ref, state);
    JS_PopGCRef(ctx, &input_ref);
    if (!valid) goto fail;
    if (op == MESH_RECEIVE) {
        state->bytes = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, MESH_MTU,
            ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
        if (!state->bytes) { JS_ThrowOutOfMemory(ctx); goto fail; }
    } else if (op == MESH_ROUTES || op == MESH_GROUPS || (op >= MESH_CONNECT && op <= MESH_FLUSH)) {
        esp32_mquickjs_wifi_mesh_control_t control = {0};
        control.kind = op == MESH_ROUTES ? ESP32_MQUICKJS_MESH_ROUTING_TABLE : op == MESH_GROUPS ?
            ESP32_MQUICKJS_MESH_GROUP_LIST : op == MESH_CONNECT ? ESP32_MQUICKJS_MESH_CONNECT :
            op == MESH_DISCONNECT ? ESP32_MQUICKJS_MESH_DISCONNECT : ESP32_MQUICKJS_MESH_FLUSH_UPSTREAM;
        control.capacity = op == MESH_ROUTES ? ESP32_MQUICKJS_MESH_MAX_NODES : op == MESH_GROUPS ? ESP32_MQUICKJS_MESH_MAX_GROUPS : 0;
        state->error = esp32_mquickjs_wifi_mesh_job_create(session, NULL, &control, state->timeout_ms, &state->job);
        if (state->error) { mesh_error(ctx, mesh_methods[op], session, state->error, false); goto fail; }
    }
    *out = state; return true;
fail:
    mesh_future_destroy(state); return false;
}

static bool mesh_future_start(JSContext *ctx, esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_future_token_t token, esp32_mquickjs_future_driver_state_t *state)
{
    (void)ctx; (void)runtime; (void)token;
    if (state->operation == MESH_CLOSE) esp32_mquickjs_wifi_mesh_session_close(state->session, false);
    else if (state->operation == MESH_RECOVER) state->error = esp32_mquickjs_wifi_mesh_session_recover(state->session);
    else if (state->operation == MESH_RECEIVE)
        state->error = esp32_mquickjs_wifi_mesh_read_claim(state->session, state->to_ds, &state->read_identity);
    else if (state->job) {
        if (state->operation == MESH_SCAN_NEXT)
            state->error = esp32_mquickjs_wifi_mesh_scan_read_claim(state->session, &state->read_identity);
        if (!state->error) state->error = esp32_mquickjs_wifi_mesh_job_submit(state->job);
    }
    (void)esp32_mquickjs_wifi_mesh_service(); return true;
}

static esp32_mquickjs_future_poll_t mesh_future_poll(esp32_mquickjs_future_driver_state_t *state)
{
    (void)esp32_mquickjs_wifi_mesh_service();
    if (state->error || state->cancelled) return ESP32_MQUICKJS_FUTURE_READY;
    if (state->job) {
        esp32_mquickjs_wifi_mesh_job_status_t result;
        esp32_mquickjs_wifi_mesh_job_result(state->job, &result, NULL);
        return result.done ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    }
    esp32_mquickjs_wifi_mesh_session_status_t status;
    esp32_mquickjs_wifi_mesh_session_status(state->session, &status);
    if (state->operation == MESH_READY)
        return status.ready || status.error || status.closing ? ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    if (state->operation == MESH_CLOSE || state->operation == MESH_RECOVER)
        return status.retired || (status.cleanup_error && !status.worker_busy && !status.recovery_pending) ?
            ESP32_MQUICKJS_FUTURE_READY : ESP32_MQUICKJS_FUTURE_PENDING;
    if (!status.ready || status.closing) { state->error = ESP_ERR_INVALID_STATE; return ESP32_MQUICKJS_FUTURE_READY; }
    if (state->message.sequence) return ESP32_MQUICKJS_FUTURE_READY;
    esp_err_t error = esp32_mquickjs_wifi_mesh_message_copy(state->session, state->to_ds,
        state->read_identity, &state->message, state->bytes, MESH_MTU);
    if (!error) return ESP32_MQUICKJS_FUTURE_READY;
    if (error == ESP_ERR_TIMEOUT) return ESP32_MQUICKJS_FUTURE_PENDING; /* Radio try-lock. */
    if (error == ESP_ERR_NOT_FOUND) {
        error = status.receive_error[state->to_ds];
        if (state->to_ds && status.native.native.native_snapshot_valid && !status.native.native.root)
            error = ESP_ERR_INVALID_STATE;
        if (!error || error == ESP_ERR_MESH_TIMEOUT || error == ESP_ERR_TIMEOUT) return ESP32_MQUICKJS_FUTURE_PENDING;
    }
    state->error = error; return ESP32_MQUICKJS_FUTURE_READY;
}

static JSValue mesh_job_error(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, bool timeout)
{
    esp32_mquickjs_wifi_mesh_session_status_t session;
    esp32_mquickjs_wifi_mesh_job_status_t job;
    esp32_mquickjs_wifi_mesh_session_status(state->session, &session);
    esp32_mquickjs_wifi_mesh_job_result(state->job, &job, NULL);
    JSGCRef ref; JSValue *details = JS_PushGCRef(ctx, &ref);
    *details = mesh_status_to_js(ctx, &session); if (JS_IsException(*details)) goto fail;
    const esp32_mquickjs_wifi_mesh_control_data_t *detail = esp32_mquickjs_wifi_mesh_job_detail(state->job);
    SET(details, "controlStage", detail && detail->stage ? JS_NewString(ctx, detail->stage) : JS_NULL);
    SET(details, "completedSteps", detail ? JS_NewUint32(ctx, detail->completed_steps) : JS_NULL);
    SET(details, "operationIdentity", JS_NewUint32(ctx, job.identity));
    SET(details, "dispatched", JS_NewBool(job.dispatched));
    SET(details, "returned", JS_NewBool(job.returned));
    SET(details, "nativeError", job.returned ? JS_NewInt32(ctx, job.native_error) : JS_NULL);
    SET(details, "waitTimedOut", JS_NewBool(timeout || job.timed_out));
    esp_err_t error = timeout ? ESP_ERR_TIMEOUT : state->error ? state->error : job.error;
    SET(details, "espCode", JS_NewInt32(ctx, error));
    SET(details, "espName", JS_NewString(ctx, esp_err_to_name(error)));
    (void)esp32_mquickjs_throw_native_error(ctx, timeout || job.timed_out ? "WIFI_MESH_TIMEOUT" : "WIFI_MESH_FAILED",
        mesh_methods[state->operation], "Mesh command failed; dispatched work may have native effects", *details);
fail:
    JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
}

static JSValue mesh_message_to_js(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    const esp32_mquickjs_wifi_mesh_message_t *m = &state->message;
    JSGCRef result_ref, view_ref;
    JSValue *result = JS_PushGCRef(ctx, &result_ref), *view = JS_PushGCRef(ctx, &view_ref);
    *result = JS_NewObject(ctx); if (JS_IsException(*result)) goto fail;
    SET(result, "sequence", JS_NewUint32(ctx, m->sequence));
    SET(result, "from", mesh_address(ctx, m->from.addr, 6));
    SET(result, "toDS", JS_NewBool(m->to_ds));
    SET(result, "ipv4", m->has_ds_address ? mesh_address(ctx, (const uint8_t *)&m->to.mip.ip4.addr, 4) : JS_NULL);
    SET(result, "port", m->has_ds_address ? JS_NewInt32(ctx, m->to.mip.port) : JS_NULL);
    SET(result, "protocol", JS_NewInt32(ctx, m->protocol));
    SET(result, "service", JS_NewInt32(ctx, m->service));
    SET(result, "flags", JS_NewInt32(ctx, m->flags));
    uint8_t *bytes = esp32_mquickjs_memory_wireless_calloc("wifi.mesh", 1, m->length ? m->length : 1,
        ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_COPY);
    if (!bytes) { JS_ThrowOutOfMemory(ctx); goto fail; }
    if (m->length) memcpy(bytes, state->bytes, m->length);
    /* The constructor adopts bytes on both success and failure. */
    *view = esp32_mquickjs_new_wireless_owned_byte_view("wifi.mesh", ctx, bytes, m->length);
    if (JS_IsException(*view)) goto fail;
    SET(result, "data", *view);
    esp_err_t error = esp32_mquickjs_wifi_mesh_message_commit(state->session, state->to_ds,
        state->read_identity, m->sequence);
    if (error) { mesh_error(ctx, "WiFiMeshSession.receive", state->session, error, false); goto fail; }
    JS_PopGCRef(ctx, &view_ref); return JS_PopGCRef(ctx, &result_ref);
fail:
    if (!JS_IsUndefined(*view) && !JS_IsException(*view)) (void)js_byte_view_close(ctx, view, 0, NULL);
    JS_PopGCRef(ctx, &view_ref); JS_PopGCRef(ctx, &result_ref); return JS_EXCEPTION;
}

static JSValue mesh_future_finish(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state)
{
    if (state->job) {
        esp32_mquickjs_wifi_mesh_job_status_t job; const mesh_addr_t *addresses = NULL;
        esp32_mquickjs_wifi_mesh_job_result(state->job, &job, &addresses);
        if (state->error || job.error || !job.done) return mesh_job_error(ctx, state, false);
        const esp32_mquickjs_wifi_mesh_control_data_t *detail = esp32_mquickjs_wifi_mesh_job_detail(state->job);
        if (detail && state->operation == MESH_SCAN) return mesh_scan_status_to_js(ctx, &detail->data.scan_result);
        if (detail && state->operation == MESH_SCAN_NEXT) return mesh_scan_record_to_js(ctx, state, detail->scan_record);
        if (detail && (state->operation == MESH_CONFIGURATION ||
            (state->operation >= MESH_HAS_GROUP && state->operation <= MESH_TSF_TIME)))
            return mesh_control_to_js(ctx, state->operation, detail);
        JSGCRef ref; JSValue *result = JS_PushGCRef(ctx, &ref);
        bool table = state->operation == MESH_ROUTES || state->operation == MESH_GROUPS || state->operation == MESH_SUBNET;
        *result = table ? JS_NewArray(ctx, 0) : JS_NewObject(ctx);
        if (JS_IsException(*result)) goto fail;
        if (table) {
            for (unsigned i = 0; i < job.count; ++i) {
                JSGCRef item_ref; JSValue *item = JS_PushGCRef(ctx, &item_ref);
                *item = mesh_address(ctx, addresses[i].addr, 6);
                bool valid = !JS_IsException(*item) && !JS_IsException(JS_SetPropertyUint32(ctx, *result, i, *item));
                JS_PopGCRef(ctx, &item_ref); if (!valid) goto fail;
            }
        } else {
            SET(result, "identity", JS_NewUint32(ctx, job.identity));
            SET(result, "bytes", JS_NewUint32(ctx, job.bytes));
            SET(result, "dispatched", JS_NewBool(job.dispatched));
            SET(result, "returned", JS_NewBool(job.returned));
            SET(result, "completedAtUs", JS_NewFloat64(ctx, job.completed_us));
        }
        return JS_PopGCRef(ctx, &ref);
fail:
        JS_PopGCRef(ctx, &ref); return JS_EXCEPTION;
    }
    if (state->error) return mesh_error(ctx, mesh_methods[state->operation], state->session, state->error, false);
    if (state->operation == MESH_RECEIVE) return mesh_message_to_js(ctx, state);
    esp32_mquickjs_wifi_mesh_session_status_t status;
    esp32_mquickjs_wifi_mesh_session_status(state->session, &status);
    if (state->operation == MESH_READY && status.ready) return mesh_status_to_js(ctx, &status);
    if (state->operation != MESH_READY && status.retired) return JS_UNDEFINED;
    return mesh_error(ctx, mesh_methods[state->operation], state->session,
        state->operation == MESH_READY ? status.error : status.cleanup_error, false);
}
static esp32_mquickjs_cancel_result_t mesh_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    state->cancelled = true;
    esp32_mquickjs_wifi_mesh_job_cancel(state->job);
    return ESP32_MQUICKJS_CANCELLED;
}
static uint32_t mesh_future_timeout(const esp32_mquickjs_future_driver_state_t *state)
{ return state->timeout_ms; }
static JSValue mesh_future_on_timeout(JSContext *ctx, esp32_mquickjs_future_driver_state_t *state, uint32_t timeout_ms)
{
    (void)timeout_ms; (void)mesh_future_cancel(state);
    if (state->operation == MESH_RECEIVE) return JS_NULL;
    if (state->job) return mesh_job_error(ctx, state, true);
    return mesh_error(ctx, mesh_methods[state->operation], state->session, ESP_ERR_TIMEOUT, true);
}
#define MESH_DRIVER(capture_fn) { .memory_owner = "wireless.future", .capture = capture_fn, .start = mesh_future_start, \
    .poll = mesh_future_poll, .finish = mesh_future_finish, .cancel = mesh_future_cancel, .destroy = mesh_future_destroy, \
    .timeout_ms = mesh_future_timeout, .on_timeout = mesh_future_on_timeout }

static JSValue mesh_call(JSContext *ctx, JSValue *receiver, int argc, JSValue *argv, const char *name)
{
    JSGCRef self_ref, method_ref;
    JSValue *self = JS_PushGCRef(ctx, &self_ref), *method = JS_PushGCRef(ctx, &method_ref);
    *self = *receiver;
    *method = JS_GetPropertyStr(ctx, *self, name);
    JSValue result = JS_IsException(*method) ? JS_EXCEPTION : esp32_mquickjs_future_call_and_wait(ctx,
        esp32_mquickjs_get_active_runtime(), *method, *self, argc, argv);
    JS_PopGCRef(ctx, &method_ref); JS_PopGCRef(ctx, &self_ref); return result;
}

static bool mesh_ready_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_READY); }
static const esp32_mquickjs_future_driver_t s_mesh_ready_driver = MESH_DRIVER(mesh_ready_capture);
JSValue js_wifi_mesh_ready(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "ready"); }
static bool mesh_close_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_CLOSE); }
static const esp32_mquickjs_future_driver_t s_mesh_close_driver = MESH_DRIVER(mesh_close_capture);
JSValue js_wifi_mesh_close(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "close"); }
static bool mesh_recover_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_RECOVER); }
static const esp32_mquickjs_future_driver_t s_mesh_recover_driver = MESH_DRIVER(mesh_recover_capture);
JSValue js_wifi_mesh_recover(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "recover"); }
static bool mesh_receive_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_RECEIVE); }
static const esp32_mquickjs_future_driver_t s_mesh_receive_driver = MESH_DRIVER(mesh_receive_capture);
JSValue js_wifi_mesh_receive(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "receive"); }
static bool mesh_send_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SEND); }
static const esp32_mquickjs_future_driver_t s_mesh_send_driver = MESH_DRIVER(mesh_send_capture);
JSValue js_wifi_mesh_send(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "send"); }
static bool mesh_routing_table_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_ROUTES); }
static const esp32_mquickjs_future_driver_t s_mesh_routing_table_driver = MESH_DRIVER(mesh_routing_table_capture);
JSValue js_wifi_mesh_routing_table(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "routingTable"); }
static bool mesh_groups_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_GROUPS); }
static const esp32_mquickjs_future_driver_t s_mesh_groups_driver = MESH_DRIVER(mesh_groups_capture);
JSValue js_wifi_mesh_groups(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "groups"); }
static bool mesh_add_groups_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_ADD_GROUPS); }
static const esp32_mquickjs_future_driver_t s_mesh_add_groups_driver = MESH_DRIVER(mesh_add_groups_capture);
JSValue js_wifi_mesh_add_groups(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "addGroups"); }
static bool mesh_remove_groups_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_REMOVE_GROUPS); }
static const esp32_mquickjs_future_driver_t s_mesh_remove_groups_driver = MESH_DRIVER(mesh_remove_groups_capture);
JSValue js_wifi_mesh_remove_groups(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "removeGroups"); }
static bool mesh_set_tods_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SET_TODS); }
static const esp32_mquickjs_future_driver_t s_mesh_set_tods_driver = MESH_DRIVER(mesh_set_tods_capture);
JSValue js_wifi_mesh_set_tods(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setToDSState"); }
static bool mesh_connect_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_CONNECT); }
static const esp32_mquickjs_future_driver_t s_mesh_connect_driver = MESH_DRIVER(mesh_connect_capture);
JSValue js_wifi_mesh_connect(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "connect"); }
static bool mesh_disconnect_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_DISCONNECT); }
static const esp32_mquickjs_future_driver_t s_mesh_disconnect_driver = MESH_DRIVER(mesh_disconnect_capture);
JSValue js_wifi_mesh_disconnect(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "disconnect"); }
static bool mesh_flush_upstream_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_FLUSH); }
static const esp32_mquickjs_future_driver_t s_mesh_flush_upstream_driver = MESH_DRIVER(mesh_flush_upstream_capture);
JSValue js_wifi_mesh_flush_upstream(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "flushUpstream"); }


static bool mesh_configuration_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_CONFIGURATION); }
static const esp32_mquickjs_future_driver_t s_mesh_configuration_driver = MESH_DRIVER(mesh_configuration_capture);
JSValue js_wifi_mesh_configuration(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "configuration"); }
static bool mesh_set_router_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SET_ROUTER); }
static const esp32_mquickjs_future_driver_t s_mesh_set_router_driver = MESH_DRIVER(mesh_set_router_capture);
JSValue js_wifi_mesh_set_router(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setRouter"); }
static bool mesh_set_mesh_id_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SET_ID); }
static const esp32_mquickjs_future_driver_t s_mesh_set_mesh_id_driver = MESH_DRIVER(mesh_set_mesh_id_capture);
JSValue js_wifi_mesh_set_mesh_id(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setMeshId"); }
static bool mesh_set_type_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SET_TYPE); }
static const esp32_mquickjs_future_driver_t s_mesh_set_type_driver = MESH_DRIVER(mesh_set_type_capture);
JSValue js_wifi_mesh_set_type(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setType"); }
static bool mesh_set_self_organized_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SELF_ORGANIZED); }
static const esp32_mquickjs_future_driver_t s_mesh_set_self_organized_driver = MESH_DRIVER(mesh_set_self_organized_capture);
JSValue js_wifi_mesh_set_self_organized(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setSelfOrganized"); }
static bool mesh_set_fixed_root_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_FIXED_ROOT); }
static const esp32_mquickjs_future_driver_t s_mesh_set_fixed_root_driver = MESH_DRIVER(mesh_set_fixed_root_capture);
JSValue js_wifi_mesh_set_fixed_root(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setFixedRoot"); }
static bool mesh_set_root_conflicts_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_ROOT_CONFLICTS); }
static const esp32_mquickjs_future_driver_t s_mesh_set_root_conflicts_driver = MESH_DRIVER(mesh_set_root_conflicts_capture);
JSValue js_wifi_mesh_set_root_conflicts(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setRootConflicts"); }
static bool mesh_set_association_expiry_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_ASSOC_EXPIRY); }
static const esp32_mquickjs_future_driver_t s_mesh_set_association_expiry_driver = MESH_DRIVER(mesh_set_association_expiry_capture);
JSValue js_wifi_mesh_set_association_expiry(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setAssociationExpiry"); }
static bool mesh_set_root_healing_delay_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_ROOT_HEALING); }
static const esp32_mquickjs_future_driver_t s_mesh_set_root_healing_delay_driver = MESH_DRIVER(mesh_set_root_healing_delay_capture);
JSValue js_wifi_mesh_set_root_healing_delay(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setRootHealingDelay"); }
static bool mesh_set_ie_encryption_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_IE_ENCRYPTION); }
static const esp32_mquickjs_future_driver_t s_mesh_set_ie_encryption_driver = MESH_DRIVER(mesh_set_ie_encryption_capture);
JSValue js_wifi_mesh_set_ie_encryption(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setIEEncryption"); }
static bool mesh_waive_root_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_WAIVE_ROOT); }
static const esp32_mquickjs_future_driver_t s_mesh_waive_root_driver = MESH_DRIVER(mesh_waive_root_capture);
JSValue js_wifi_mesh_waive_root(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "waiveRoot"); }
static bool mesh_switch_channel_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SWITCH_CHANNEL); }
static const esp32_mquickjs_future_driver_t s_mesh_switch_channel_driver = MESH_DRIVER(mesh_switch_channel_capture);
JSValue js_wifi_mesh_switch_channel(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "switchChannel"); }
static bool mesh_set_device_duty_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_DEVICE_DUTY); }
static const esp32_mquickjs_future_driver_t s_mesh_set_device_duty_driver = MESH_DRIVER(mesh_set_device_duty_capture);
JSValue js_wifi_mesh_set_device_duty(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setDeviceDuty"); }
static bool mesh_set_network_duty_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_NETWORK_DUTY); }
static const esp32_mquickjs_future_driver_t s_mesh_set_network_duty_driver = MESH_DRIVER(mesh_set_network_duty_capture);
JSValue js_wifi_mesh_set_network_duty(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setNetworkDuty"); }
static bool mesh_signal_duty_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SIGNAL_DUTY); }
static const esp32_mquickjs_future_driver_t s_mesh_signal_duty_driver = MESH_DRIVER(mesh_signal_duty_capture);
JSValue js_wifi_mesh_signal_duty(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "signalDuty"); }
static bool mesh_subnet_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SUBNET); }
static const esp32_mquickjs_future_driver_t s_mesh_subnet_driver = MESH_DRIVER(mesh_subnet_capture);
JSValue js_wifi_mesh_subnet(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "subnet"); }
static bool mesh_has_group_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_HAS_GROUP); }
static const esp32_mquickjs_future_driver_t s_mesh_has_group_driver = MESH_DRIVER(mesh_has_group_capture);
JSValue js_wifi_mesh_has_group(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "hasGroup"); }
static bool mesh_upstream_capacity_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_UPSTREAM_CAPACITY); }
static const esp32_mquickjs_future_driver_t s_mesh_upstream_capacity_driver = MESH_DRIVER(mesh_upstream_capacity_capture);
JSValue js_wifi_mesh_upstream_capacity(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "upstreamCapacity"); }
static bool mesh_power_status_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_POWER_STATUS); }
static const esp32_mquickjs_future_driver_t s_mesh_power_status_driver = MESH_DRIVER(mesh_power_status_capture);
JSValue js_wifi_mesh_power_status(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "powerStatus"); }
static bool mesh_tsf_time_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_TSF_TIME); }
static const esp32_mquickjs_future_driver_t s_mesh_tsf_time_driver = MESH_DRIVER(mesh_tsf_time_capture);
JSValue js_wifi_mesh_tsf_time(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "tsfTime"); }

static bool mesh_set_parent_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SET_PARENT); }
static const esp32_mquickjs_future_driver_t s_mesh_set_parent_driver = MESH_DRIVER(mesh_set_parent_capture);
JSValue js_wifi_mesh_set_parent(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "setParent"); }
static bool mesh_scan_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SCAN); }
static const esp32_mquickjs_future_driver_t s_mesh_scan_driver = MESH_DRIVER(mesh_scan_capture);
JSValue js_wifi_mesh_scan(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "scan"); }
static bool mesh_receive_scan_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SCAN_NEXT); }
static const esp32_mquickjs_future_driver_t s_mesh_receive_scan_driver = MESH_DRIVER(mesh_receive_scan_capture);
JSValue js_wifi_mesh_receive_scan(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "receiveScan"); }
static bool mesh_flush_scan_capture(JSContext *ctx, JSGCRef *self, int argc, JSGCRef *argv, esp32_mquickjs_future_driver_state_t **out)
{ return mesh_future_capture(ctx, self, argc, argv, out, MESH_SCAN_FLUSH); }
static const esp32_mquickjs_future_driver_t s_mesh_flush_scan_driver = MESH_DRIVER(mesh_flush_scan_capture);
JSValue js_wifi_mesh_flush_scan(JSContext *ctx, JSValue *self, int argc, JSValue *argv)
{ return mesh_call(ctx, self, argc, argv, "flushScan"); }

#undef MESH_DRIVER
