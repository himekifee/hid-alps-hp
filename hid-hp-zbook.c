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

/* USB */
#define USB_VENDOR_ID_ZBOOK 0x1FC9
#define USB_PRODUCT_ID_ZBOOK 0x0088

/* Bluetooth */
#define BLUETOOTH_VENDOR_ID_ZBOOK 0x04F2
#define BLUETOOTH_PRODUCT_ID_ZBOOK 0x1573

#define U1_ABSOLUTE_REPORT_ID 0x07 /* Absolute data ReportID */

#define MAX_TOUCHES 5

enum dev_num {
    TOUCHPAD,
    KEYBOARD,
    FN_KEY,
    PROG_BTN,
    UNKNOWN,
};
/**
 * struct zbook_dev
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
struct zbook_dev {
    struct input_dev *input;
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
};

static int zbook_raw_event(struct hid_device *hdev, struct hid_report *report,
                           u8 *data, int size) {
    struct zbook_dev *hdata = hid_get_drvdata(hdev);
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
                    input_report_abs(hdata->input, ABS_MT_PRESSURE,
                                     z);
                } else {
                    input_mt_report_slot_inactive(hdata->input);
                }
            }

            input_mt_sync_frame(hdata->input);

            input_report_key(hdata->input, BTN_LEFT, data[1] & 0x1);
            input_report_key(hdata->input, BTN_RIGHT, (data[1] & 0x2));
            input_report_key(hdata->input, BTN_MIDDLE, (data[1] & 0x4));

            input_sync(hdata->input);

            return 1;
    }

    return 0;
}

static int __maybe_unused

zbook_post_reset(struct hid_device *hdev) {
    return -1;
}

static int __maybe_unused

zbook_post_resume(struct hid_device *hdev) {
    return zbook_post_reset(hdev);
}

static int touchpad_init(struct hid_device *hdev, struct zbook_dev *pri_data) {
    pri_data->max_fingers = 5;
    pri_data->btn_cnt = 1;
    pri_data->has_sp = 0;
    pri_data->x_max = 3328;
    pri_data->y_min = 1;
    pri_data->y_max = 1920;
    pri_data->x_active_len_mm = 111;
    pri_data->y_active_len_mm = 66;
    return 0;
}

static int zbook_input_configured(struct hid_device *hdev, struct hid_input *hi) {
    int ret;
    struct zbook_dev *data = hid_get_drvdata(hdev);
    if ((hdev->vendor == USB_VENDOR_ID_ZBOOK &&
         hdev->product == USB_PRODUCT_ID_ZBOOK && // USB keyboard
         strstr(hdev->phys, "input3")) || // Touchpad interface
        (hdev->vendor == BLUETOOTH_VENDOR_ID_ZBOOK &&
         hdev->product == BLUETOOTH_PRODUCT_ID_ZBOOK // Bluetooth keyboard
        )) {
        if (data->dev_type == TOUCHPAD) {
            struct input_dev *input = hi->input;
            int res_x, res_y, i;

            data->input = input;

            hid_dbg(hdev, "Opening low level driver\n");
            ret = hid_hw_open(hdev);
            if (ret)
                return ret;

            /* Allow incoming hid reports */
            hid_device_io_start(hdev);
            ret = touchpad_init(hdev, data);

            if (ret)
                goto exit;

            __set_bit(EV_ABS, input->evbit);
            input_set_abs_params(input, ABS_MT_POSITION_X,
                                 data->x_min, data->x_max, 0, 0);
            input_set_abs_params(input, ABS_MT_POSITION_Y,
                                 data->y_min, data->y_max, 0, 0);

            if (data->x_active_len_mm && data->y_active_len_mm) {
                res_x = (data->x_max - 1) /
                        data->x_active_len_mm;
                res_y = (data->y_max - 1) /
                        data->y_active_len_mm;

                input_abs_set_res(input, ABS_MT_POSITION_X,
                                  res_x);
                input_abs_set_res(input, ABS_MT_POSITION_Y,
                                  res_y);
            }

            input_set_abs_params(input, ABS_MT_PRESSURE, 0, 127, 0,
                                 0);

            input_mt_init_slots(input, data->max_fingers,
                                INPUT_MT_POINTER);

            __set_bit(EV_KEY, input->evbit);

            if (data->btn_cnt == 1)
                __set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

            for (i = 0; i < data->btn_cnt; i++)
                __set_bit(BTN_LEFT + i, input->keybit);
        }
    }
    return 0;

    exit:
    hid_device_io_stop(hdev);
    hid_hw_close(hdev);
    return ret;
}

static int zbook_input_mapping(struct hid_device *hdev, struct hid_input *hi,
                               struct hid_field *field, struct hid_usage *usage,
                               unsigned long **bit, int *max) {
    if ((hdev->vendor == USB_VENDOR_ID_ZBOOK &&
         hdev->product == USB_PRODUCT_ID_ZBOOK && // USB keyboard
         !strstr(hdev->phys, "input3")) ||
        (hdev->vendor == BLUETOOTH_VENDOR_ID_ZBOOK &&
         hdev->product ==
         BLUETOOTH_PRODUCT_ID_ZBOOK && // Bluetooth keyboard
         (hi->name == NULL ||
          (hi->name != NULL &&
           !strstr(hi->name, "Mouse"))))) // Other interfaces
        return 0;
    return -1;
}

static int zbook_probe(struct hid_device *hdev, const struct hid_device_id *id) {
    int ret;

    struct zbook_dev *data = NULL;
    data = devm_kzalloc(&hdev->dev, sizeof(struct zbook_dev), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->hdev = hdev;
    if ((hdev->vendor == USB_VENDOR_ID_ZBOOK &&
         hdev->product == USB_PRODUCT_ID_ZBOOK && // USB keyboard
         strstr(hdev->phys, "input3")) || // Touchpad interface
        (hdev->vendor == BLUETOOTH_VENDOR_ID_ZBOOK &&
         hdev->product == BLUETOOTH_PRODUCT_ID_ZBOOK // Bluetooth keyboard
        )) {
        hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;
        if (hdev->vendor == BLUETOOTH_VENDOR_ID_ZBOOK &&
            hdev->product ==
            BLUETOOTH_PRODUCT_ID_ZBOOK) // Bluetooth keyboard
            hdev->quirks |= HID_QUIRK_INPUT_PER_APP;

        ret = hid_parse(hdev);
        if (ret) {
            hid_err(hdev, "parse failed\n");
            return ret;
        }

        hid_set_drvdata(hdev, data);
        switch (hdev->product) {
            case USB_PRODUCT_ID_ZBOOK:
            case BLUETOOTH_PRODUCT_ID_ZBOOK:
                data->dev_type = TOUCHPAD;
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

    hdev->quirks |= HID_QUIRK_INPUT_PER_APP;
    ret = hid_parse(hdev);
    if (ret)
        return ret;

    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (ret) {
        hid_err(hdev, "hw start failed\n");
        return ret;
    }

    return 0;
}

static void zbook_remove(struct hid_device *hdev) {
    hid_hw_stop(hdev);
}

static const struct hid_device_id zbook_id[] = {
        {HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY, USB_VENDOR_ID_ZBOOK,
                    USB_PRODUCT_ID_ZBOOK)},
        {HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY, BLUETOOTH_VENDOR_ID_ZBOOK,
                    BLUETOOTH_PRODUCT_ID_ZBOOK)},
        {}
};

MODULE_DEVICE_TABLE(hid, zbook_id
);

static struct hid_driver zbook_driver = {
        .name = "hid-hp-zbook",
        .id_table = zbook_id,
        .probe = zbook_probe,
        .remove = zbook_remove,
        .raw_event = zbook_raw_event,
        .input_mapping = zbook_input_mapping,
        .input_configured = zbook_input_configured,
#ifdef CONFIG_PM
        .resume = zbook_post_resume,
    .reset_resume = zbook_post_reset,
#endif
};

module_hid_driver(zbook_driver);

MODULE_AUTHOR("Grider Li <himekifee@gmail.com>");
MODULE_DESCRIPTION("HP ZBook x2 G4 HID driver");
MODULE_LICENSE("GPL");
