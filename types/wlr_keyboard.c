#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include "types/wlr_keyboard.h"
#include "util/array.h"
#include "util/shm.h"
#include "util/signal.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
// Функция для отправки JSON-сообщения на сервер
void send_layout_change(const char *layout_name, const char *variant);
void send_layout_change(const char *layout_name, const char *variant) {
    // Получаем домашний каталог пользователя
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Error: HOME environment variable not set.\n");
        return;
    }

    // Формируем путь к сокету
    char socket_path[512];
    snprintf(socket_path, sizeof(socket_path), "%s/.config/newm/sockets/lang.sock", home_dir);

    // Создаём сокет
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return;
    }

    // Задаём адрес сокета
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Подключаемся к сокету
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sockfd);
        return;
    }

    // Формируем JSON-сообщение
    char json_message[256];
    snprintf(json_message, sizeof(json_message),
             "{\"command\": \"set_lang\", \"data\": {\"layout\": \"%s\", \"variant\":\"%s\", \"class\": \"lang_neutral\"}}",
             layout_name, variant);

    // Отправляем JSON-сообщение
    if (write(sockfd, json_message, strlen(json_message)) == -1) {
        perror("write");
        close(sockfd);
        return;
    }

    // Читаем ответ от сервера
    char buffer[1024];
    ssize_t num_bytes = read(sockfd, buffer, sizeof(buffer) - 1);
    if (num_bytes == -1) {
        perror("read");
        close(sockfd);
        return;
    }
    buffer[num_bytes] = '\0';
    printf("Received response: %s\n", buffer);

    // Закрываем сокет
    close(sockfd);
}
void send_socket_lang(const char *layout_name);
void send_socket_lang(const char *layout_name){
	char layout_name_copy[256];
		char variant[32] = "none";  // Значение по умолчанию
		if (layout_name) {
			strncpy(layout_name_copy, layout_name, sizeof(layout_name_copy) - 1);
			layout_name_copy[sizeof(layout_name_copy) - 1] = '\0';
		} else {
			strncpy(layout_name_copy, "unknown", sizeof(layout_name_copy));
		}
		// Извлекаем основное название (до скобки, если она есть)
		char *short_name = strtok(layout_name_copy, "(");
		if (short_name) {
			// Убираем пробелы в конце, если они есть
			short_name[strcspn(short_name, " ")] = '\0';
		} else {
			short_name = layout_name_copy;
		}
		// Ищем вариант (текст в скобках)
		char *open_bracket = strchr(layout_name, '(');
		if (open_bracket) {
			char *close_bracket = strchr(open_bracket, ')');
			if (close_bracket && close_bracket > open_bracket + 1) {
				size_t len = close_bracket - open_bracket - 1;
				if (len < sizeof(variant)) {
					strncpy(variant, open_bracket + 1, len);
					variant[len] = '\0';
				}
			}
		}

		if (layout_name) {
			send_layout_change(short_name,variant);
		}	
}
void keyboard_led_update(struct wlr_keyboard *keyboard) {
	if (keyboard->xkb_state == NULL) {
		return;
	}

	uint32_t leds = 0;
	for (uint32_t i = 0; i < WLR_LED_COUNT; ++i) {
		if (xkb_state_led_index_is_active(keyboard->xkb_state,
				keyboard->led_indexes[i])) {
			leds |= (1 << i);
		}
	}
	wlr_keyboard_led_update(keyboard, leds);
}

/**
 * Update the modifier state of the wlr-keyboard. Returns true if the modifier
 * state changed.
 */
bool keyboard_modifier_update(struct wlr_keyboard *keyboard) {
	if (keyboard->xkb_state == NULL) {
		return false;
	}

	xkb_mod_mask_t depressed = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_DEPRESSED);
	xkb_mod_mask_t latched = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_LATCHED);
	xkb_mod_mask_t locked = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_LOCKED);
	xkb_mod_mask_t group = xkb_state_serialize_layout(keyboard->xkb_state,
		XKB_STATE_LAYOUT_EFFECTIVE);
	
	if (group != keyboard->modifiers.group) {
		const char *layout_name = xkb_keymap_layout_get_name(keyboard->keymap, group);
		send_socket_lang(layout_name);
	}

	if (depressed == keyboard->modifiers.depressed &&
			latched == keyboard->modifiers.latched &&
			locked == keyboard->modifiers.locked &&
			group == keyboard->modifiers.group) {
		return false;
	}

	keyboard->modifiers.depressed = depressed;
	keyboard->modifiers.latched = latched;
	keyboard->modifiers.locked = locked;
	keyboard->modifiers.group = group;

	return true;
}

void keyboard_key_update(struct wlr_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		set_add(keyboard->keycodes, &keyboard->num_keycodes,
			WLR_KEYBOARD_KEYS_CAP, event->keycode);
	}
	if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		set_remove(keyboard->keycodes, &keyboard->num_keycodes,
			WLR_KEYBOARD_KEYS_CAP, event->keycode);
	}

	assert(keyboard->num_keycodes <= WLR_KEYBOARD_KEYS_CAP);
}

void wlr_keyboard_notify_modifiers(struct wlr_keyboard *keyboard,
		uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
		uint32_t group) {
	if (keyboard->xkb_state == NULL) {
		return;
	}
	xkb_state_update_mask(keyboard->xkb_state, mods_depressed, mods_latched,
		mods_locked, 0, 0, group);

	bool updated = keyboard_modifier_update(keyboard);
	if (updated) {
		wlr_signal_emit_safe(&keyboard->events.modifiers, keyboard);
	}

	keyboard_led_update(keyboard);
}

void wlr_keyboard_notify_key(struct wlr_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	keyboard_key_update(keyboard, event);
	wlr_signal_emit_safe(&keyboard->events.key, event);

	if (keyboard->xkb_state == NULL) {
		return;
	}

	if (event->update_state) {
		uint32_t keycode = event->keycode + 8;
		xkb_state_update_key(keyboard->xkb_state, keycode,
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
	}

	bool updated = keyboard_modifier_update(keyboard);
	if (updated) {
		wlr_signal_emit_safe(&keyboard->events.modifiers, keyboard);
	}
	keyboard_led_update(keyboard);
}

void wlr_keyboard_init(struct wlr_keyboard *kb,
		const struct wlr_keyboard_impl *impl, const char *name) {
	wlr_input_device_init(&kb->base, WLR_INPUT_DEVICE_KEYBOARD, name);
	kb->base.keyboard = kb;

	kb->impl = impl;
	wl_signal_init(&kb->events.key);
	wl_signal_init(&kb->events.modifiers);
	wl_signal_init(&kb->events.keymap);
	wl_signal_init(&kb->events.repeat_info);
	wl_signal_init(&kb->events.destroy);

	kb->keymap_fd = -1;

	// Sane defaults
	kb->repeat_info.rate = 25;
	kb->repeat_info.delay = 600;
}

void wlr_keyboard_destroy(struct wlr_keyboard *kb) {
	if (kb == NULL) {
		return;
	}
	wlr_signal_emit_safe(&kb->events.destroy, kb);
	wlr_input_device_finish(&kb->base);

	xkb_state_unref(kb->xkb_state);
	xkb_keymap_unref(kb->keymap);
	free(kb->keymap_string);
	if (kb->keymap_fd >= 0) {
		close(kb->keymap_fd);
	}
	if (kb->impl && kb->impl->destroy) {
		kb->impl->destroy(kb);
	} else {
		wl_list_remove(&kb->events.key.listener_list);
		free(kb);
	}
}

void wlr_keyboard_led_update(struct wlr_keyboard *kb, uint32_t leds) {
	if (kb->impl && kb->impl->led_update) {
		kb->impl->led_update(kb, leds);
	}
}

bool wlr_keyboard_set_keymap(struct wlr_keyboard *kb,
		struct xkb_keymap *keymap) {
	xkb_keymap_unref(kb->keymap);
	kb->keymap = xkb_keymap_ref(keymap);

	xkb_state_unref(kb->xkb_state);
	kb->xkb_state = xkb_state_new(kb->keymap);
	if (kb->xkb_state == NULL) {
		wlr_log(WLR_ERROR, "Failed to create XKB state");
		goto err;
	}

	const char *led_names[WLR_LED_COUNT] = {
		XKB_LED_NAME_NUM,
		XKB_LED_NAME_CAPS,
		XKB_LED_NAME_SCROLL,
	};
	for (size_t i = 0; i < WLR_LED_COUNT; ++i) {
		kb->led_indexes[i] = xkb_map_led_get_index(kb->keymap, led_names[i]);
	}

	const char *mod_names[WLR_MODIFIER_COUNT] = {
		XKB_MOD_NAME_SHIFT,
		XKB_MOD_NAME_CAPS,
		XKB_MOD_NAME_CTRL, // "Control"
		XKB_MOD_NAME_ALT, // "Mod1"
		XKB_MOD_NAME_NUM, // "Mod2"
		"Mod3",
		XKB_MOD_NAME_LOGO, // "Mod4"
		"Mod5",
	};
	// TODO: there's also "Ctrl", "Alt"?
	for (size_t i = 0; i < WLR_MODIFIER_COUNT; ++i) {
		kb->mod_indexes[i] = xkb_map_mod_get_index(kb->keymap, mod_names[i]);
	}

	char *tmp_keymap_string = xkb_keymap_get_as_string(kb->keymap,
		XKB_KEYMAP_FORMAT_TEXT_V1);
	if (tmp_keymap_string == NULL) {
		wlr_log(WLR_ERROR, "Failed to get string version of keymap");
		goto err;
	}
	free(kb->keymap_string);
	kb->keymap_string = tmp_keymap_string;
	kb->keymap_size = strlen(kb->keymap_string) + 1;

	int rw_fd = -1, ro_fd = -1;
	if (!allocate_shm_file_pair(kb->keymap_size, &rw_fd, &ro_fd)) {
		wlr_log(WLR_ERROR, "Failed to allocate shm file for keymap");
		goto err;
	}

	void *dst = mmap(NULL, kb->keymap_size, PROT_READ | PROT_WRITE,
		MAP_SHARED, rw_fd, 0);
	if (dst == MAP_FAILED) {
		wlr_log_errno(WLR_ERROR, "mmap failed");
		close(rw_fd);
		close(ro_fd);
		goto err;
	}

	memcpy(dst, kb->keymap_string, kb->keymap_size);
	munmap(dst, kb->keymap_size);
	close(rw_fd);

	if (kb->keymap_fd >= 0) {
		close(kb->keymap_fd);
	}
	kb->keymap_fd = ro_fd;

	for (size_t i = 0; i < kb->num_keycodes; ++i) {
		xkb_keycode_t keycode = kb->keycodes[i] + 8;
		xkb_state_update_key(kb->xkb_state, keycode, XKB_KEY_DOWN);
	}

	keyboard_modifier_update(kb);

	wlr_signal_emit_safe(&kb->events.keymap, kb);
	const char *layout_name = xkb_keymap_layout_get_name(kb->keymap, 0);
	send_socket_lang(layout_name);
	return true;

err:
	xkb_state_unref(kb->xkb_state);
	kb->xkb_state = NULL;
	xkb_keymap_unref(keymap);
	kb->keymap = NULL;
	free(kb->keymap_string);
	kb->keymap_string = NULL;
	return false;
}

void wlr_keyboard_set_repeat_info(struct wlr_keyboard *kb, int32_t rate,
		int32_t delay) {
	if (kb->repeat_info.rate == rate && kb->repeat_info.delay == delay) {
		return;
	}
	kb->repeat_info.rate = rate;
	kb->repeat_info.delay = delay;
	wlr_signal_emit_safe(&kb->events.repeat_info, kb);
}

uint32_t wlr_keyboard_get_modifiers(struct wlr_keyboard *kb) {
	xkb_mod_mask_t mask = kb->modifiers.depressed | kb->modifiers.latched;
	uint32_t modifiers = 0;
	for (size_t i = 0; i < WLR_MODIFIER_COUNT; ++i) {
		if (kb->mod_indexes[i] != XKB_MOD_INVALID &&
				(mask & (1 << kb->mod_indexes[i]))) {
			modifiers |= (1 << i);
		}
	}
	return modifiers;
}

bool wlr_keyboard_keymaps_match(struct xkb_keymap *km1,
		struct xkb_keymap *km2) {
	if (!km1 && !km2) {
		return true;
	}
	if (!km1 || !km2) {
		return false;
	}
	char *km1_str = xkb_keymap_get_as_string(km1, XKB_KEYMAP_FORMAT_TEXT_V1);
	char *km2_str = xkb_keymap_get_as_string(km2, XKB_KEYMAP_FORMAT_TEXT_V1);
	bool result = strcmp(km1_str, km2_str) == 0;
	free(km1_str);
	free(km2_str);
	return result;
}
