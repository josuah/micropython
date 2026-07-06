/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Panoramix Labs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "modzephyr.h"
#include "py/runtime.h"

#include <zephyr/logging/log.h>
#include <zephyr/drivers/video.h>

#include <zephyr/mp/mp.h>
#include <zephyr/mp/mp_caps.h>
#include <zephyr/mp/mp_fake_src.h>
#include <zephyr/mp/mp_sink.h>
#include <zephyr/mp/mp_src.h>
#include <zephyr/mp/mp_structure.h>
#include <zephyr/mp/mp_transform.h>
#ifdef CONFIG_MP_BASE_APPSINK
#include <zephyr/mp/base/mp_appsink.h>
#endif
#ifdef CONFIG_MP_VID
#include <zephyr/mp/vid/mp_vid_convert.h>
#include <zephyr/mp/vid/mp_vid_property.h>
#include <zephyr/mp/vid/mp_vid_src.h>
#include <zephyr/mp/vid/mp_vid_transform.h>
#include <zephyr/mp/vid/mp_vid_transform_client.h>
#endif

#ifdef CONFIG_MP

LOG_MODULE_REGISTER(mp_zephyr_mediapipe, LOG_LEVEL_DBG);

static uint32_t mediapipe_element_id;

#define MP_ELEMENT_INIT_AUTO_ID(elem, initfn) \
    ({ MP_ELEMENT_INIT(elem, initfn, mediapipe_element_id); mediapipe_element_id++; })

// zephyr.mediapipe.Element()

typedef struct _mediapipe_element_obj_t {
    mp_obj_base_t base;
    const char *name;
    union {
        struct mp_element element;
        struct mp_fake_src fake_src;
        struct mp_sink sink;
        struct mp_transform transform;
        #if CONFIG_MP_BASE_APPSINK
        struct mp_appsink appsink;
        #endif
        #if CONFIG_MP_VID
        struct mp_vid_src vid_src;
        struct mp_vid_transform vid_transform;
        struct mp_vid_transform vid_convert;
        struct mp_vid_transform vid_transform_client;
        #endif
    };
    mp_obj_t hook_fn;
} mediapipe_element_obj_t;

static void mediapipe_element_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mediapipe_element_obj_t *self = self_in;
    mp_printf(print, "Element('%s', id=%u)", self->name, ((struct mp_object *)&self->element)->id);
}

static mp_obj_t mediapipe_element_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mediapipe_element_obj_t *self = mp_obj_malloc(mediapipe_element_obj_t, type);
    self->name = mp_obj_str_get_str(args[0]);
    self->hook_fn = mp_const_none;

    /* core */

    if (strcmp(self->name, "fake_src") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_fake_src_init);
    } else if (strcmp(self->name, "sink") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_sink_init);
    } else if (strcmp(self->name, "transform") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_transform_init);

    /* base */

    #if CONFIG_MP_BASE_APPSINK
    } else if (strcmp(self->name, "appsink") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_appsink_init);
    #endif

    /* vid */

    #if CONFIG_MP_VID
    } else if (strcmp(self->name, "vid_src") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_vid_src_init);
    } else if (strcmp(self->name, "vid_transform") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_vid_transform_init);
    #endif

    #if CONFIG_MP_VID_CONVERT
    } else if (strcmp(self->name, "vid_convert") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_vid_convert_init);
    #endif

    #if CONFIG_MP_VID && CONFIG_MP_RPC
    } else if (strcmp(self->name, "vid_transform_client") == 0) {
        MP_ELEMENT_INIT_AUTO_ID(&self->element, mp_vid_transform_client_init);
    #endif

    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("Unsupported element type"));
    }

    mp_object_ref(&self->element.object);

    return MP_OBJ_FROM_PTR(self);
}

static int mediapipe_element_hook(struct mp_element *element, uint8_t pad_idx) {
    mediapipe_element_obj_t *self = CONTAINER_OF(element, mediapipe_element_obj_t, element);
    mp_sched_schedule(self->hook_fn, mp_const_none);
    return 0;
}

static mp_obj_t mediapipe_element___del__(mp_obj_t self_in) {
    mediapipe_element_obj_t *self = self_in;
    mp_object_unref(&self->element.object);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mediapipe_element___del___obj, mediapipe_element___del__);

static mp_obj_t mediapipe_element_prop(size_t n_args, const mp_obj_t *args) {
    mediapipe_element_obj_t *self = args[0];
    mp_int_t key = mp_obj_get_int(args[1]);
    enum { UNKNOWN, INT, DEVICE, CROP, FUNC } type = UNKNOWN;
    void *val;
    int ret;

    if (strcmp(self->name, "fake_src") == 0) {
        type =
            (key == MP_PROP_SRC_NUM_BUFS) ? INT :
            UNKNOWN;
    #ifdef CONFIG_MP_VID
    } else if (strncmp(self->name, "vid_", 4) == 0) {
        type =
            (key == MP_PROP_SRC_NUM_BUFS) ? INT :
            (key == MP_PROP_VID_DEVICE) ? DEVICE :
            (key == MP_PROP_VID_CROP) ? CROP :
            UNKNOWN;
    #endif
    #ifdef CONFIG_MP_BASE
    } else if (strcmp(self->name, "appsink") == 0) {
        type =
            (key == MP_APPSINK_PROP_HOOK) ? FUNC :
            UNKNOWN;
    #endif
    } else {
        mp_raise_TypeError(MP_ERROR_TEXT("This element has no properties"));
    }

    if (n_args == 3) {
        switch (type) {
        case INT:
            val = (void *)mp_obj_get_int(args[2]);
            break;
        case DEVICE:
            val = (void *)device_get_binding(mp_obj_str_get_str(args[2]));
            if (val == NULL) {
                mp_raise_ValueError(MP_ERROR_TEXT("Device not found"));
            }
            break;
        case CROP:
            struct video_rect val_rect;
            mp_obj_t *items;
            mp_obj_get_array_fixed_n(args[2], 4, &items);
            val_rect.left = mp_obj_get_int(items[0]);
            val_rect.top = mp_obj_get_int(items[1]);
            val_rect.width = mp_obj_get_int(items[2]);
            val_rect.height = mp_obj_get_int(items[3]);
            val = &val_rect;
            break;
        case FUNC:
            self->hook_fn = args[2];
            val = mediapipe_element_hook;
            break;
        case UNKNOWN:
            LOG_WRN("set UNKNOWN");
            mp_raise_TypeError(MP_ERROR_TEXT("Unknown property type"));
        }

        ret = mp_object_set_properties(&self->element.object, key, val, MP_PROP_LIST_END);
        if (ret < 0) {
            mp_raise_OSError(-ret);
        }
    } else {
        ret = mp_object_get_properties(&self->element.object, key, &val, MP_PROP_LIST_END);
        if (ret < 0) {
            mp_raise_OSError(-ret);
        }

        switch (type) {
        case INT:
            return mp_obj_new_int((uintptr_t)val);
        case DEVICE:
            return mp_obj_new_str_from_cstr(((const struct device *)val)->name);
        case CROP:
            mp_obj_t tuple[4] = {
                mp_obj_new_int_from_uint(((struct video_rect *)val)->left),
                mp_obj_new_int_from_uint(((struct video_rect *)val)->top),
                mp_obj_new_int_from_uint(((struct video_rect *)val)->width),
                mp_obj_new_int_from_uint(((struct video_rect *)val)->height),
            };
            return mp_obj_new_tuple(4, tuple);
        case FUNC:
            return self->hook_fn;
        case UNKNOWN:
            mp_raise_ValueError(MP_ERROR_TEXT("Unsupported property type"));
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mediapipe_element_prop_obj, 2, 3, mediapipe_element_prop);

static mp_obj_t mediapipe_value_to_obj(mp_value_t value) {
    switch (mp_value_get_type(value)) {
    case MP_TYPE_BOOLEAN:
        return mp_value_get_boolean(value) ? mp_const_true : mp_const_false;
    case MP_TYPE_ENUM:
    case MP_TYPE_INT:
        return mp_obj_new_int_from_ll(mp_value_get_int(value));
    case MP_TYPE_RANGE:
        mp_obj_t tuple[3] = {
            mp_obj_new_int_from_ll(mp_value_get_range_min(value)),
            mp_obj_new_int_from_ll(mp_value_get_range_max(value)),
            mp_obj_new_int_from_ll(mp_value_get_range_step(value)),
        };
        return mp_obj_new_tuple(3, tuple);
    case MP_TYPE_STRING:
        return mp_obj_new_str_from_cstr(mp_value_get_string(value));
    case MP_TYPE_LIST:
        mp_obj_t list = mp_obj_new_list(0, NULL);
        mp_value_t elem;
        for (int i = 0; (elem = mp_value_list_get(value, i)) != NULL; i++) { 
            mp_obj_list_append(list, mediapipe_value_to_obj(elem));
        }
        return list;
    default:
        return mp_const_none;
    }
}

static void mediapipe_structure_set_field(struct mp_structure *structure, uint8_t field_id, mp_map_t *map, int qstr) {
    mp_value_t value = mp_structure_get_value(structure, field_id);
    if (value != NULL) {
        mp_obj_t key = MP_OBJ_NEW_QSTR(qstr);
        mp_map_elem_t *elem = mp_map_lookup(map, key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
        elem->value = mediapipe_value_to_obj(value);
    }
}

static mp_obj_t mediapipe_structure_to_dict(struct mp_structure *structure) {
    mp_obj_t dict = mp_obj_new_dict(0);
    mp_map_t *map = mp_obj_dict_get_map(dict);

    mediapipe_structure_set_field(structure, MP_CAPS_PIXEL_FORMAT, map, MP_QSTR_pixel_format);
    mediapipe_structure_set_field(structure, MP_CAPS_IMAGE_WIDTH, map, MP_QSTR_image_width);
    mediapipe_structure_set_field(structure, MP_CAPS_IMAGE_HEIGHT, map, MP_QSTR_image_height);
    mediapipe_structure_set_field(structure, MP_CAPS_FRAME_RATE, map, MP_QSTR_frame_rate);
    mediapipe_structure_set_field(structure, MP_CAPS_SAMPLE_RATE, map, MP_QSTR_sample_rate);
    mediapipe_structure_set_field(structure, MP_CAPS_BITWIDTH, map, MP_QSTR_bitwidth);
    mediapipe_structure_set_field(structure, MP_CAPS_NUM_OF_CHANNEL, map, MP_QSTR_num_of_channel);
    mediapipe_structure_set_field(structure, MP_CAPS_FRAME_INTERVAL, map, MP_QSTR_frame_interval);
    mediapipe_structure_set_field(structure, MP_CAPS_BUFFER_COUNT, map, MP_QSTR_buffer_count);

    map->is_fixed = 1;

    return dict;
}

static mp_obj_t mediapipe_element_caps(mp_obj_t self_in, mp_obj_t pad_idx_in, mp_obj_t cap_idx_in) {
    mediapipe_element_obj_t *self = self_in;

    struct mp_object *object;
    SYS_DLIST_FOR_EACH_CONTAINER(&self->element.srcpads, object, node) {
        if (object->id == mp_obj_get_int(pad_idx_in)) {
            goto found;
        }
    }
    SYS_DLIST_FOR_EACH_CONTAINER(&self->element.sinkpads, object, node) {
        if (object->id == mp_obj_get_int(pad_idx_in)) {
            goto found;
        }
    }
    return mp_const_none;

found:
    struct mp_pad *pad = (struct mp_pad *)object;
    if (pad->caps == NULL) {
        return mp_const_none;
    }

    struct mp_structure *structure = mp_caps_get_structure(pad->caps, mp_obj_get_int(cap_idx_in));
    if (structure == NULL) {
        return mp_const_none;
    }

    mp_obj_t dict = mediapipe_structure_to_dict(structure);
    mp_structure_unref(structure);
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_3(mediapipe_element_caps_obj, mediapipe_element_caps);

static const mp_rom_map_elem_t mediapipe_element_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mediapipe_element___del___obj) },
    { MP_ROM_QSTR(MP_QSTR_prop), MP_ROM_PTR(&mediapipe_element_prop_obj) },
    { MP_ROM_QSTR(MP_QSTR_caps), MP_ROM_PTR(&mediapipe_element_caps_obj) },
};
static MP_DEFINE_CONST_DICT(mediapipe_element_locals_dict, mediapipe_element_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    mediapipe_element_type,
    MP_QSTR_Element,
    MP_TYPE_FLAG_NONE,
    make_new, mediapipe_element_make_new,
    print, mediapipe_element_print,
    locals_dict, &mediapipe_element_locals_dict
    );

// zephyr.mediapipe.Pipeline()

typedef struct _mediapipe_pipeline_obj_t {
    mp_obj_base_t base;
    struct mp_pipeline pipeline;
} mediapipe_pipeline_obj_t;

static void mediapipe_pipeline_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mediapipe_pipeline_obj_t *self = self_in;
    mp_printf(print, "Pipeline(id=%u)", ((struct mp_object *)&self->pipeline)->id);
}

static mp_obj_t mediapipe_pipeline_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, false);
    mediapipe_pipeline_obj_t *self = mp_obj_malloc(mediapipe_pipeline_obj_t, type);
    int ret;

    MP_ELEMENT_INIT_AUTO_ID(&self->pipeline, mp_pipeline_init);

    for (size_t i = 0; i < n_args; i++) {
        mediapipe_element_obj_t *obj = (void *)args[i];

        if (mp_obj_get_type(obj) != &mediapipe_element_type) {
            mp_raise_TypeError("Expected Element() arguments");
        }

        ret = mp_bin_add((struct mp_bin *)&self->pipeline, (struct mp_element *)&obj->element, NULL);
        if (ret < 0) {
            mp_raise_OSError(-ret);
        }
    }

    mp_object_ref((struct mp_object *)&self->pipeline);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t mediapipe_pipeline___del__(mp_obj_t self_in) {
    mediapipe_pipeline_obj_t *self = self_in;

    LOG_INF("mediapipe_pipeline___del__");

    /* Releases some of the resources */
    mp_element_set_state((struct mp_element *)&self->pipeline, MP_STATE_READY);

    /* Release the rest of the resources */
    mp_object_unref((struct mp_object *)&self->pipeline);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mediapipe_pipeline___del___obj, mediapipe_pipeline___del__);

static mp_obj_t mediapipe_pipeline_link(size_t n_args, const mp_obj_t *args) {
    int ret;

    for (size_t i = 1; i + 1 < n_args; i++) {
        mediapipe_element_obj_t *obj0 = (void *)args[i + 0];
        mediapipe_element_obj_t *obj1 = (void *)args[i + 1];

        if (mp_obj_get_type(obj0) != &mediapipe_element_type ||
            mp_obj_get_type(obj1) != &mediapipe_element_type) {
            mp_raise_TypeError("Expected Element() arguments");
        }

        ret = mp_element_link(&obj0->element, &obj1->element, NULL);
        if (ret < 0) {
            mp_raise_OSError(-ret);
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(mediapipe_pipeline_link_obj, 3, mediapipe_pipeline_link);

static mp_obj_t mediapipe_pipeline_state(size_t n_args, const mp_obj_t *args) {
    mediapipe_pipeline_obj_t *self = args[0];
    int ret;

    if (n_args == 2) {
        mp_int_t state = mp_obj_get_int(args[1]);

        ret = mp_element_set_state((struct mp_element *)&self->pipeline, state);
        if (ret != MP_STATE_CHANGE_SUCCESS) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("Failed to apply state"));
        }

        return mp_const_none;
    } else {
        return MP_OBJ_NEW_SMALL_INT(((struct mp_element *)&self->pipeline)->current_state);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mediapipe_pipeline_state_obj, 1, 2, mediapipe_pipeline_state);

static mp_obj_t mediapipe_pipeline_pop_msg(mp_obj_t self_in, mp_obj_t mask_in) {
    mediapipe_pipeline_obj_t *self = self_in;
    mp_int_t mask = mp_obj_get_int(mask_in);
    struct mp_bus *bus;
    struct mp_message msg;

    bus = mp_element_get_bus((struct mp_element *)&self->pipeline);
    mp_bus_peek(bus, &msg);

    if ((msg.type & mask) != 0) {
        mp_bus_pop(bus, &msg);
        return MP_OBJ_NEW_SMALL_INT(msg.type);
    } else {
        return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(mediapipe_pipeline_pop_msg_obj, mediapipe_pipeline_pop_msg);

static const mp_rom_map_elem_t mediapipe_pipeline_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mediapipe_pipeline___del___obj) },
    { MP_ROM_QSTR(MP_QSTR_link), MP_ROM_PTR(&mediapipe_pipeline_link_obj) },
    { MP_ROM_QSTR(MP_QSTR_state), MP_ROM_PTR(&mediapipe_pipeline_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_pop_msg), MP_ROM_PTR(&mediapipe_pipeline_pop_msg_obj) },
};
static MP_DEFINE_CONST_DICT(mediapipe_pipeline_locals_dict, mediapipe_pipeline_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    mediapipe_pipeline_type,
    MP_QSTR_Element,
    MP_TYPE_FLAG_NONE,
    make_new, mediapipe_pipeline_make_new,
    print, mediapipe_pipeline_print,
    locals_dict, &mediapipe_pipeline_locals_dict
    );

// zephyr.mediapipe

static mp_obj_t mediapipe_heap_stats(void) {
    extern struct k_heap _system_heap;
    struct sys_memory_stats stats;
    sys_heap_runtime_stats_get(&_system_heap.heap, &stats);
    return mp_obj_new_int(stats.allocated_bytes);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mediapipe_heap_stats_obj, mediapipe_heap_stats);

static const mp_rom_map_elem_t mp_module_zephyr_mediapipe_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_Element), MP_ROM_PTR(&mediapipe_element_type) },
    { MP_ROM_QSTR(MP_QSTR_Pipeline), MP_ROM_PTR(&mediapipe_pipeline_type) },
    { MP_ROM_QSTR(MP_QSTR_heap_stats), MP_ROM_PTR(&mediapipe_heap_stats_obj) },
    { MP_ROM_QSTR(MP_QSTR_PROP_SRC_NUM_BUFS), MP_ROM_INT(MP_PROP_SRC_NUM_BUFS) },
    #ifdef CONFIG_MP_VID
    { MP_ROM_QSTR(MP_QSTR_PROP_VID_DEVICE), MP_ROM_INT(MP_PROP_VID_DEVICE) },
    { MP_ROM_QSTR(MP_QSTR_PROP_VID_CROP), MP_ROM_INT(MP_PROP_VID_CROP) },
    #endif
    #ifdef CONFIG_MP_BASE
    { MP_ROM_QSTR(MP_QSTR_PROP_APPSINK_HOOK), MP_ROM_INT(MP_APPSINK_PROP_HOOK) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_STATE_READY), MP_ROM_INT(MP_STATE_READY) },
    { MP_ROM_QSTR(MP_QSTR_STATE_PAUSED), MP_ROM_INT(MP_STATE_PAUSED) },
    { MP_ROM_QSTR(MP_QSTR_STATE_PLAYING), MP_ROM_INT(MP_STATE_PLAYING) },
    { MP_ROM_QSTR(MP_QSTR_MESSAGE_UNKNOWN), MP_ROM_INT(MP_MESSAGE_UNKNOWN) },
    { MP_ROM_QSTR(MP_QSTR_MESSAGE_EOS), MP_ROM_INT(MP_MESSAGE_EOS) },
    { MP_ROM_QSTR(MP_QSTR_MESSAGE_ERROR), MP_ROM_INT(MP_MESSAGE_ERROR) },
    { MP_ROM_QSTR(MP_QSTR_MESSAGE_WARNING), MP_ROM_INT(MP_MESSAGE_WARNING) },
};
static MP_DEFINE_CONST_DICT(mp_module_zephyr_mediapipe_globals, mp_module_zephyr_mediapipe_globals_table);

const mp_obj_module_t mp_module_zephyr_mediapipe = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_zephyr_mediapipe_globals,
};

MP_REGISTER_MODULE(MP_QSTR_zephyr_mediapipe, mp_module_zephyr_mediapipe);

#endif // CONFIG_MP
