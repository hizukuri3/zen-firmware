/*
 * Derive the base layer from the active Bluetooth profile.
 *
 * The Mac and the Windows host need different modifier positions and different
 * symbol spellings, so each one has its own base layer. Pairing that layer with
 * the profile by hand does not survive a reset: ZMK keeps the layer state in
 * RAM only, while the active profile is restored from settings. After a deep
 * sleep or a power cycle the keyboard therefore comes back on layer 0 while
 * still connected to Windows, and types a Mac layout at a JIS host until the
 * profile key is pressed again.
 *
 * Making the layer a function of the profile removes that failure mode: the
 * profile is the single stored fact, and the layer is recomputed from it on
 * every profile change and once at startup.
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(zen_profile_layers, CONFIG_ZMK_LOG_LEVEL);

/*
 * Base layer for each Bluetooth profile, by layer index. Profile 0 is the Mac
 * and profile 1 the JIS Windows host; the spare profiles fall back to the Mac
 * layer. Keep this in step with config/keymap.keymap.
 */
static const zmk_keymap_layer_index_t profile_base_layer[] = {0, 1, 0, 0, 0};

static void apply_profile_layer(int profile) {
    if (profile < 0 || profile >= (int)ARRAY_SIZE(profile_base_layer)) {
        LOG_WRN("No base layer mapped for profile %d", profile);
        return;
    }

    zmk_keymap_layer_index_t index = profile_base_layer[profile];
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);

    if (id == ZMK_KEYMAP_LAYER_ID_INVAL) {
        LOG_WRN("Profile %d wants layer %d, which the keymap does not have", profile, index);
        return;
    }

    /* zmk_keymap_layer_to leaves exactly the default layer and the target
     * active, so the target being the highest active layer means there is
     * nothing to do. Skipping the call keeps a held momentary layer alive when
     * the profile "changes" to the one already selected. */
    if (zmk_keymap_highest_layer_active() == index) {
        return;
    }

    LOG_DBG("Profile %d selects base layer %d", profile, index);
    zmk_keymap_layer_to(id);
}

static int profile_layers_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev = as_zmk_ble_active_profile_changed(eh);

    if (ev != NULL) {
        apply_profile_layer(ev->index);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zen_profile_layers, profile_layers_listener);
ZMK_SUBSCRIPTION(zen_profile_layers, zmk_ble_active_profile_changed);

/*
 * The profile-changed event is not raised while settings are being loaded, so
 * a keyboard that boots without its host in range would sit on the wrong layer
 * until the first connection. Read the restored profile once, late enough for
 * the settings subsystem to have run.
 */
static void startup_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    apply_profile_layer(zmk_ble_active_profile_index());
}

static K_WORK_DELAYABLE_DEFINE(startup_work, startup_work_cb);

static int profile_layers_init(void) {
    k_work_schedule(&startup_work, K_MSEC(CONFIG_ZEN_PROFILE_LAYERS_STARTUP_DELAY_MS));
    return 0;
}

SYS_INIT(profile_layers_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
