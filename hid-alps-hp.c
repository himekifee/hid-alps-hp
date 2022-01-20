// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2022 Grider Li <himekifee@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <asm/unaligned.h>

/* ALPS Device Product ID */
#define USB_VENDOR_ID_ALPS_HP        0x1FC9
#define HID_DEVICE_ID_ALPS_U1_HP     0x0088

#define U1_ABSOLUTE_REPORT_ID        0x07 /* Absolute data ReportID */

#define MAX_TOUCHES    5

enum dev_num {
    U1,
    UNKNOWN,
};
/**
 * struct alps_dev
 *
 * @input: pointer to the kernel input device
 * @input2: pointer to the kernel input2 device
 * @hdev: pointer to the struct hid_device
 *
 * @dev_type: device type
 * @max_fingers: total number of fingers
 * @has_sp: boolean of sp existense
 * @sp_btn_info: button information
 * @x_active_len_mm: active area length of X (mm)
 * @y_active_len_mm: active area length of Y (mm)
 * @x_max: maximum x coordinate value
 * @y_max: maximum y coordinate value
 * @x_min: minimum x coordinate value
 * @y_min: minimum y coordinate value
 * @btn_cnt: number of buttons
 * @sp_btn_cnt: number of stick buttons
 */
struct alps_dev {
    struct input_dev *input;
    struct input_dev *input2;
    struct hid_device *hdev;

    enum dev_num dev_type;
    u8 max_fingers;
    u8 has_sp;
    u8 sp_btn_info;
    u32 x_active_len_mm;
    u32 y_active_len_mm;
    u32 x_max;
    u32 y_max;
    u32 x_min;
    u32 y_min;
    u32 btn_cnt;
    u32 sp_btn_cnt;
};

static int alps_raw_event(struct hid_device *hdev,
                          struct hid_report *report, u8 *data, int size) {
    struct alps_dev *hdata = hid_get_drvdata(hdev);
    unsigned int x, y, z;
    int i;

    if (!data)
        return 0;
    switch (data[0]) {
        case U1_ABSOLUTE_REPORT_ID:
            for (i = 0; i < hdata->max_fingers; i++) {
                u8 *contact = &data[i * 5];

                x = get_unaligned_le16(contact + 3);
                y = get_unaligned_le16(contact + 5);
                z = contact[7] & 0x7F;

                input_mt_slot(hdata->input, i);

                if (z != 0) {
                    input_mt_report_slot_state(hdata->input,
                                               MT_TOOL_FINGER, 1);
                    input_report_abs(hdata->input,
                                     ABS_MT_POSITION_X, x);
                    input_report_abs(hdata->input,
                                     ABS_MT_POSITION_Y, y);
                    input_report_abs(hdata->input,
                                     ABS_MT_PRESSURE, z);
                } else {
                    input_mt_report_slot_inactive(hdata->input);
                }
            }

            input_mt_sync_frame(hdata->input);

            input_report_key(hdata->input, BTN_LEFT,
                             data[1] & 0x1);
            input_report_key(hdata->input, BTN_RIGHT,
                             (data[1] & 0x2));
            input_report_key(hdata->input, BTN_MIDDLE,
                             (data[1] & 0x4));

            input_sync(hdata->input);

            return 1;
    }

    return 0;
}

static int __maybe_unused

alps_post_reset(struct hid_device *hdev) {
    return -1;
}

static int __maybe_unused

alps_post_resume(struct hid_device *hdev) {
    return alps_post_reset(hdev);
}

static int u1_init(struct hid_device *hdev, struct alps_dev *pri_data) {
    pri_data->max_fingers = 5;
    pri_data->btn_cnt = 1;
    pri_data->has_sp = 0;
    pri_data->x_max = 3328;
    pri_data->y_min = 1;
    pri_data->y_max = 1920;
    pri_data->x_active_len_mm = 110;
    pri_data->y_active_len_mm = 65;
    return 0;
}

static int alps_input_configured(struct hid_device *hdev, struct hid_input *hi) {
    struct alps_dev *data = hid_get_drvdata(hdev);
    struct input_dev *input = hi->input;
    int ret;
    int res_x, res_y, i;

    data->input = input;

    hid_dbg(hdev, "Opening low level driver\n");
    ret = hid_hw_open(hdev);
    if (ret)
        return ret;

    /* Allow incoming hid reports */
    hid_device_io_start(hdev);
    ret = u1_init(hdev, data);


    if (ret)
        goto exit;

    __set_bit(EV_ABS, input->evbit);
    input_set_abs_params(input, ABS_MT_POSITION_X,
                         data->x_min, data->x_max, 0, 0);
    input_set_abs_params(input, ABS_MT_POSITION_Y,
                         data->y_min, data->y_max, 0, 0);

    if (data->x_active_len_mm && data->y_active_len_mm) {
        res_x = (data->x_max - 1) / data->x_active_len_mm;
        res_y = (data->y_max - 1) / data->y_active_len_mm;

        input_abs_set_res(input, ABS_MT_POSITION_X, res_x);
        input_abs_set_res(input, ABS_MT_POSITION_Y, res_y);
    }

    input_set_abs_params(input, ABS_MT_PRESSURE, 0, 127, 0, 0);

    input_mt_init_slots(input, data->max_fingers, INPUT_MT_POINTER);

    __set_bit(EV_KEY, input->evbit);

    if (data->btn_cnt == 1)
        __set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

    for (i = 0; i < data->btn_cnt; i++)
        __set_bit(BTN_LEFT + i, input->keybit);

    exit:
    hid_device_io_stop(hdev);
    hid_hw_close(hdev);
    return ret;
}

static int alps_input_mapping(struct hid_device *hdev,
                              struct hid_input *hi, struct hid_field *field,
                              struct hid_usage *usage, unsigned long **bit, int *max) {
    return -1;
}

static int alps_probe(struct hid_device *hdev, const struct hid_device_id *id) {
    struct alps_dev *data = NULL;
    int ret;
    data = devm_kzalloc(&hdev->dev, sizeof(struct alps_dev), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->hdev = hdev;
    hid_set_drvdata(hdev, data);

    hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

    ret = hid_parse(hdev);
    if (ret) {
        hid_err(hdev, "parse failed\n");
        return ret;
    }

    switch (hdev->product) {
        case HID_DEVICE_ID_ALPS_U1_HP:
            data->dev_type = U1;
            break;
        default:
            data->dev_type = UNKNOWN;
    }

    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (ret) {
        hid_err(hdev, "hw start failed\n");
        return ret;
    }

    return 0;
}

static void alps_remove(struct hid_device *hdev) {
    hid_hw_stop(hdev);
}

static const struct hid_device_id alps_id[] = {
        { HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY,
                    USB_VENDOR_ID_ALPS_HP, HID_DEVICE_ID_ALPS_U1_HP) },
        { }
};

MODULE_DEVICE_TABLE(hid, alps_id
);

static struct hid_driver alps_driver = {
        .name = "hid-alps-hp",
        .id_table        = alps_id,
        .probe            = alps_probe,
        .remove            = alps_remove,
        .raw_event        = alps_raw_event,
        .input_mapping        = alps_input_mapping,
        .input_configured    = alps_input_configured,
#ifdef CONFIG_PM
        .resume			= alps_post_resume,
    .reset_resume		= alps_post_reset,
#endif
};

module_hid_driver(alps_driver);

MODULE_AUTHOR("Grider Li <himekifee@gmail.com>");
MODULE_DESCRIPTION("ALPS HP specific HID driver");
MODULE_LICENSE("GPL");