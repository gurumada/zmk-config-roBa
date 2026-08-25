#define DT_DRV_COMPAT gurumada_input_processor_key_guard

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define KEY_GUARD_NODE DT_DRV_INST(0)
#define POST_RELEASE_MS DT_PROP_OR(KEY_GUARD_NODE, post_release_ms, 500)

static struct k_spinlock state_lock;
static uint16_t pressed_count;
static atomic_t suppressed;

#if DT_NODE_HAS_PROP(KEY_GUARD_NODE, excluded_positions)
static const uint32_t excluded_positions[] = DT_PROP(KEY_GUARD_NODE, excluded_positions);

static bool is_excluded_position(uint32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(excluded_positions); i++) {
        if (excluded_positions[i] == position) {
            return true;
        }
    }

    return false;
}
#else
static bool is_excluded_position(uint32_t position) { return false; }
#endif

static void release_suppression(struct k_work *work) {
    ARG_UNUSED(work);

    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (pressed_count == 0) {
        atomic_set(&suppressed, 0);
    }
    k_spin_unlock(&state_lock, key);
}

K_WORK_DELAYABLE_DEFINE(release_work, release_suppression);

static int key_guard_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event == NULL || is_excluded_position(event->position)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool cancel_release = false;
    bool schedule_release = false;
    k_spinlock_key_t key = k_spin_lock(&state_lock);

    if (event->state) {
        pressed_count++;
        atomic_set(&suppressed, 1);
        cancel_release = true;
    } else if (pressed_count > 0) {
        pressed_count--;
        schedule_release = pressed_count == 0;
    }

    k_spin_unlock(&state_lock, key);

    if (cancel_release) {
        (void)k_work_cancel_delayable(&release_work);
    } else if (schedule_release) {
        (void)k_work_reschedule(&release_work, K_MSEC(POST_RELEASE_MS));
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_guard_position, key_guard_position_listener);
ZMK_SUBSCRIPTION(key_guard_position, zmk_position_state_changed);

static bool is_pointer_event(const struct input_event *event) {
    if (event->type != INPUT_EV_REL) {
        return false;
    }

    return event->code == INPUT_REL_X || event->code == INPUT_REL_Y ||
           event->code == INPUT_REL_WHEEL || event->code == INPUT_REL_HWHEEL;
}

static int key_guard_handle_event(const struct device *dev, struct input_event *event,
                                  uint32_t param1, uint32_t param2,
                                  struct zmk_input_processor_state *state) {
    ARG_UNUSED(dev);
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (atomic_get(&suppressed) && is_pointer_event(event)) {
        event->value = 0;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api key_guard_driver_api = {
    .handle_event = key_guard_handle_event,
};

static int key_guard_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

#define KEY_GUARD_INST(n)                                                                          \
    DEVICE_DT_INST_DEFINE(n, key_guard_init, NULL, NULL, NULL, POST_KERNEL,                         \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &key_guard_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KEY_GUARD_INST)

#endif
