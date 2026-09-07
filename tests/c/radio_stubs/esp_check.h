#define ESP_RETURN_ON_ERROR(call, tag, message) do { int e = (call); if (e) return e; } while (0)
