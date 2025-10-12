/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Analog Devices, Inc.
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

#include "zephyr_device.h"
#include "py/runtime.h"

#if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL)

#include <zephyr/drivers/pinctrl.h>
#include "extmod/modmachine.h"
#include "modmachine.h"

#define PINCTRL_EXISTS_X_OR_Y(node, to_do, to_or) COND_CODE_1(DT_CAT(node, _PINCTRL_IDX_0_EXISTS), \
    (to_do), (to_or))

#define LABEL_EXISTS_X_OR_Y(node, to_do, to_or) COND_CODE_0(DT_NUM_NODELABELS(node), \
    (to_do), (to_or))

#define DT_NODELABEL_STRING_ARRAY_DECLARE(node) static const char *_##node##_labels[] = \
    DT_NODELABEL_STRING_ARRAY(node)

#if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS)

#define DT_NODELABEL_STRING_ARRAY_DECLARE_PINCTRL(node) \
    LABEL_EXISTS_X_OR_Y(node, , DT_NODELABEL_STRING_ARRAY_DECLARE(node);)

#else

#define DT_NODELABEL_STRING_ARRAY_DECLARE_PINCTRL(node) \
    PINCTRL_EXISTS_X_OR_Y(node, DT_NODELABEL_STRING_ARRAY_DECLARE(node);, )

#endif

DT_FOREACH_STATUS_OKAY_NODE(DT_NODELABEL_STRING_ARRAY_DECLARE_PINCTRL)

#define DT_NODELABEL_STRING_ARRAY_NAME(node) \
    LABEL_EXISTS_X_OR_Y(node, NULL, _##node##_labels)

#if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS)

/* Without pinctrl they may not actually exist, it is impossible to tell before link time
 * and there is no way around weak symbols at all here.
 */
#define DEVICE_DATA_WEAK_IMPL(node) \
    extern const struct device DEVICE_DT_NAME_GET(node) __weak;

#define DEVICE_DATA_ENTRY_IMPL(node) {                   \
        .dev = DEVICE_DT_GET(node),                      \
        .name = DEVICE_DT_NAME(node),                    \
        .labels = DT_NODELABEL_STRING_ARRAY_NAME(node),  \
        .label_cnt = DT_NUM_NODELABELS(node),            \
},

#define DEVICE_DATA_WEAK(node) \
    PINCTRL_EXISTS_X_OR_Y(node, , DEVICE_DATA_WEAK_IMPL(node))

DT_FOREACH_STATUS_OKAY_NODE(DEVICE_DATA_WEAK)

#endif

#if defined(CONFIG_MICROPY_DYNAMIC_PINCTRL)

#define PINCTRL_DT_DEV_CONFIG_ENTRY(node) \
    PINCTRL_EXISTS_X_OR_Y(node, PINCTRL_DT_DEV_CONFIG_GET(node), NULL)

#define PINCTRL_DT_DATA_DECLARE(node) \
    PINCTRL_EXISTS_X_OR_Y(node, PINCTRL_DT_DEV_CONFIG_DECLARE(node); , )

DT_FOREACH_STATUS_OKAY_NODE(PINCTRL_DT_DATA_DECLARE)

#define DEVICE_DATA_ENTRY_IMPL_PINCTRL(node) {           \
        .dev = DEVICE_DT_GET(node),                      \
        .name = DEVICE_DT_NAME(node),                    \
        .labels = DT_NODELABEL_STRING_ARRAY_NAME(node),  \
        .label_cnt = DT_NUM_NODELABELS(node),            \
        .pinctrl_cfg = PINCTRL_DT_DEV_CONFIG_ENTRY(node) \
},

#endif /* CONFIG_MICROPY_DYNAMIC_PINCTRL */

#if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS)

#define DEVICE_DATA_ENTRY(node) \
    PINCTRL_EXISTS_X_OR_Y(node, DEVICE_DATA_ENTRY_IMPL_PINCTRL(node), DEVICE_DATA_ENTRY_IMPL(node))

#else

#define DEVICE_DATA_ENTRY(node) \
    PINCTRL_EXISTS_X_OR_Y(node, DEVICE_DATA_ENTRY_IMPL_PINCTRL(node), )

#endif


static const mp_zephyr_device_data_t all_devices[] =
{
    DT_FOREACH_STATUS_OKAY_NODE(DEVICE_DATA_ENTRY)
};

const mp_zephyr_device_data_t *device_find_by_node_label(const char *name) {
    for (int i = 0; i < ARRAY_SIZE(all_devices); i++)
    {
        for (int j = 0; j < all_devices[i].label_cnt; j++) {
            if (strcmp(name, all_devices[i].labels[j]) == 0) {
                return &(all_devices[i]);
            }
        }
    }
    return NULL;
}

const mp_zephyr_device_data_t *device_find_by_name(const char *name) {
    for (int i = 0; i < ARRAY_SIZE(all_devices); i++)
    {
        if (strcmp(name, all_devices[i].name) == 0) {
            return &(all_devices[i]);
        }
    }
    return NULL;
}

const mp_zephyr_device_data_t *device_find_by_API(const char *api_kind, size_t id) {
    for (int i = 0; i < ARRAY_SIZE(all_devices); i++)
    {
        enum zephyr_api_kind kind = device_get_api_kind(all_devices[i].dev);
        if (kind > 0) {
            const char *api = device_get_api_str(kind);
            if (api != NULL) {
                if (strcmp(api_kind, api) == 0 && id == 0) {
                    return &(all_devices[i]);
                } else {
                    id--;
                }
            }
        }
    }
    return NULL;
}

const mp_zephyr_device_data_t *device_find_by_id(size_t id) {
    if (id < ARRAY_SIZE(all_devices)) {
        return &all_devices[id];
    }
    return NULL;
}

#endif /* defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL) */

const struct device *zephyr_device_find(mp_obj_t ident) {
    #if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL)
    if (mp_obj_is_type(ident, &mp_type_tuple)) {
        mp_obj_t *items;
        mp_obj_get_array_fixed_n(ident, 2, &items);

        const mp_zephyr_device_data_t *device = device_find_by_API(mp_obj_str_get_str(items[0]), mp_obj_get_int(items[1]));
        if (device == NULL) {
            mp_raise_ValueError(MP_ERROR_TEXT("device not found"));
        } else {
            if (device->dev == NULL) {
                mp_raise_ValueError(MP_ERROR_TEXT("device is invalid"));
            }
            return device->dev;
        }
    }
    #endif

    const char *dev_name = mp_obj_str_get_str(ident);
    const struct device *dev = device_get_binding(dev_name);


    #ifdef CONFIG_DEVICE_DT_METADATA
    if (dev == NULL) {
        dev = device_get_by_dt_nodelabel(dev_name);
    }
    #endif

    #if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL)
    const mp_zephyr_device_data_t *device;

    if (dev == NULL) {
        device = device_find_by_node_label(dev_name);
        if (device == NULL) {
            device = device_find_by_name(dev_name);
        }
        if (device != NULL) {
            if (device->dev == NULL) {
                #if MICROPY_ERROR_REPORTING <= MICROPY_ERROR_REPORTING_TERSE
                mp_raise_ValueError(MP_ERROR_TEXT("device is invalid"));
                #else
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("device %s is invalid"), dev_name);
                #endif
            }
            dev = device->dev;
        }
    }
    #endif

    if (dev == NULL) {
        #if MICROPY_ERROR_REPORTING <= MICROPY_ERROR_REPORTING_TERSE
        mp_raise_ValueError(MP_ERROR_TEXT("device not found"));
        #else
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("device %s not found"), dev_name);
        #endif
    }

    return dev;
}

const char *zephyr_device_get_name(const struct device *dev) {
    #ifdef CONFIG_DEVICE_DT_METADATA
    const struct device_dt_nodelabels *nl = device_get_dt_nodelabels(dev);

    if (nl != NULL && nl->num_nodelabels > 0) {
        return nl->nodelabels[0];
    }
    #endif

    return dev->name;
}

#if defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL)

const mp_zephyr_device_data_t *zephyr_device_find_data_dev(const struct device *dev) {
    for (int i = 0; i < ARRAY_SIZE(all_devices); i++)
    {
        if (all_devices[i].dev == dev) {
            return &(all_devices[i]);
        }
    }

    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("unexpected critical failure"));

    return 0;
}

#if defined(CONFIG_MICROPY_DYNAMIC_PINCTRL)

#if defined(CONFIG_MICROPY_DYNAMIC_PINCTRL_GPIO_PREVIOUS_DIRECT) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL_GPIO_PREVIOUS_UPDATE)
#include "dynamic_pinctrl/builder_api.h"
#endif

int zephyr_device_apply_pinctrl(const mp_zephyr_device_data_t *data, size_t pin_cnt, ...) {
    pinctrl_soc_pin_t *pinctrl;
    struct pinctrl_state *pinctrl_states;
    va_list args;
    pinctrl_soc_pin_t pinctrl_buffer[32];
    int ret = 0;
    size_t real_pin_cnt = 0;
    bool need_off = false;

    if (data->pinctrl_cfg == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("device is invalid (no pinctrl)"));
    }

    va_start(args, pin_cnt);

    for (int i = 0; i < pin_cnt; i++) {
        mp_obj_t pin_obj = va_arg(args, mp_obj_t);
        if (pin_obj == mp_const_none) {
            continue;
        }
        if (!mp_obj_is_type(pin_obj, &machine_pin_type)) {
            mp_raise_ValueError(MP_ERROR_TEXT("argument is not a Pin"));
        }
        machine_pin_obj_t *pin = MP_OBJ_TO_PTR(pin_obj);
        if (!pin->is_pinctrl) {
            mp_raise_ValueError(MP_ERROR_TEXT("Pin is not a pinctrl Pin"));
        }
        pinctrl_buffer[real_pin_cnt] = pin->pinctrl;
        real_pin_cnt++;
    }

    va_end(args);

    if (device_is_ready(data->dev)) {
        ret = device_deinit(data->dev);
        need_off = true;
    }

    if (ret == -ENOTSUP) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("device is already initialized and doesn't support de-initialization"));
    }
    if (ret < 0) {
        mp_raise_OSError(-ret);
    }

    #ifdef CONFIG_MICROPY_DYNAMIC_PINCTRL_GPIO_PREVIOUS_DIRECT
    if (need_off) {
        pinctrl = m_malloc(data->pinctrl_cfg->states[0].pin_cnt * sizeof(pinctrl_soc_pin_t));
        for (int i = 0; i < data->pinctrl_cfg->states[0].pin_cnt; i++) {
            pinctrl[i] = dynamic_pinctrl_builder_api.set_pinctrl_off(data->pinctrl_cfg->states[0].pins[i]);
        }

        ret = pinctrl_configure_pins(pinctrl, data->pinctrl_cfg->states[0].pin_cnt, (uintptr_t)NULL);
        if (ret < 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("couldn't apply pinctrl off"));
        }
        m_free(pinctrl);
    }
    #endif

    #ifdef CONFIG_MICROPY_DYNAMIC_PINCTRL_GPIO_PREVIOUS_UPDATE
    pinctrl = m_malloc((real_pin_cnt + data->pinctrl_cfg->states[0].pin_cnt) * sizeof(pinctrl_soc_pin_t));
    #else
    pinctrl = m_malloc(real_pin_cnt * sizeof(pinctrl_soc_pin_t));
    #endif
    pinctrl_states = m_malloc(data->pinctrl_cfg->state_cnt * sizeof(struct pinctrl_state));

    for (int i = 0; i < real_pin_cnt; i++) {
        pinctrl[i] = pinctrl_buffer[i];
    }

    #ifdef CONFIG_MICROPY_DYNAMIC_PINCTRL_GPIO_PREVIOUS_UPDATE
    for (int i = real_pin_cnt; i < (real_pin_cnt + data->pinctrl_cfg->states[0].pin_cnt); i++) {
        pinctrl[i] = dynamic_pinctrl_builder_api.set_pinctrl_off(data->pinctrl_cfg->states[0].pins[i - real_pin_cnt]);
    }
    #endif

    for (int i = 0; i < data->pinctrl_cfg->state_cnt; i++) {
        pinctrl_states[i].pins = pinctrl;
        #ifdef CONFIG_MICROPY_DYNAMIC_PINCTRL_GPIO_PREVIOUS_UPDATE
        if (need_off) {
            pinctrl_states[i].pin_cnt = real_pin_cnt + data->pinctrl_cfg->states[0].pin_cnt;
        } else {
            pinctrl_states[i].pin_cnt = real_pin_cnt;
        }
        #else
        pinctrl_states[i].pin_cnt = real_pin_cnt;
        #endif
        pinctrl_states[i].id = data->pinctrl_cfg->states[i].id;
    }

    ret = pinctrl_update_states(data->pinctrl_cfg, pinctrl_states, data->pinctrl_cfg->state_cnt);
    if (ret < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("couldn't apply pinctrl"));
    }

    ret = device_init(data->dev);
    if (ret < 0) {
        mp_raise_OSError(-ret);
    }

    return ret;
}

#endif /* CONFIG_MICROPY_DYNAMIC_PINCTRL */

#endif /* defined(CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS) || defined(CONFIG_MICROPY_DYNAMIC_PINCTRL) */
