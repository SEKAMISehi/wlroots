#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/types/wlr_pointer.h>

void wlr_pointer_init(struct wlr_pointer *pointer,
		const struct wlr_pointer_impl *impl, const char *name) {
	wlr_input_device_init(&pointer->base, WLR_INPUT_DEVICE_POINTER, name);
	pointer->base.pointer = pointer;

	pointer->impl = impl;
	wl_signal_init(&pointer->events.motion);
	wl_signal_init(&pointer->events.motion_absolute);
	wl_signal_init(&pointer->events.button);
	wl_signal_init(&pointer->events.axis);
	wl_signal_init(&pointer->events.frame);
	wl_signal_init(&pointer->events.swipe_begin);
	wl_signal_init(&pointer->events.swipe_update);
	wl_signal_init(&pointer->events.swipe_end);
	wl_signal_init(&pointer->events.pinch_begin);
	wl_signal_init(&pointer->events.pinch_update);
	wl_signal_init(&pointer->events.pinch_end);
	wl_signal_init(&pointer->events.hold_begin);
	wl_signal_init(&pointer->events.hold_end);
}

void wlr_pointer_destroy(struct wlr_pointer *pointer) {
	if (!pointer) {
		return;
	}
	wlr_input_device_finish(&pointer->base);
	if (pointer->impl && pointer->impl->destroy) {
		pointer->impl->destroy(pointer);
	} else {
		free(pointer);
	}
}
