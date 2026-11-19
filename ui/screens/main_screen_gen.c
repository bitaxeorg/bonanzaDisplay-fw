/**
 * @file main_screen_gen.c
 * @brief Bonanza Pixel Studio layout from main_screen.xml.
 */

#include "main_screen_gen.h"
#include "bonanza_ui.h"

lv_obj_t * main_screen_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t studio_screen;
    static lv_style_t studio_1;
    static lv_style_t studio_2;
    static lv_style_t studio_3;
    static lv_style_t studio_4;
    static lv_style_t studio_5;
    static lv_style_t studio_6;
    static lv_style_t studio_7;
    static lv_style_t studio_8;
    static lv_style_t studio_9;
    static lv_style_t studio_10;
    static lv_style_t studio_11;
    static lv_style_t studio_12;
    static lv_style_t studio_13;
    static lv_style_t studio_14;
    static lv_style_t studio_15;
    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&studio_screen);
        lv_style_set_bg_color(&studio_screen, lv_color_hex(0x000000));
        lv_style_set_bg_opa(&studio_screen, (255 * 100 / 100));
        lv_style_set_border_width(&studio_screen, 0);
        lv_style_set_pad_all(&studio_screen, 0);

        lv_style_init(&studio_1);
        lv_style_set_text_font(&studio_1, portfolio_6x8);
        lv_style_set_text_color(&studio_1, lv_color_hex(0x333333));

        lv_style_init(&studio_2);
        lv_style_set_text_font(&studio_2, portfolio_6x8);
        lv_style_set_text_color(&studio_2, lv_color_hex(0xaaaaaa));

        lv_style_init(&studio_3);
        lv_style_set_text_font(&studio_3, portfolio_6x8);
        lv_style_set_text_color(&studio_3, lv_color_hex(0xaaaaaa));

        lv_style_init(&studio_4);
        lv_style_set_text_font(&studio_4, fmtowns_8x16);
        lv_style_set_text_color(&studio_4, lv_color_hex(0xffffff));

        lv_style_init(&studio_5);
        lv_style_set_text_font(&studio_5, portfolio_6x8);
        lv_style_set_text_color(&studio_5, lv_color_hex(0x222222));

        lv_style_init(&studio_6);
        lv_style_set_text_font(&studio_6, fmtowns_8x16);
        lv_style_set_text_color(&studio_6, lv_color_hex(0xffffff));

        lv_style_init(&studio_7);
        lv_style_set_text_font(&studio_7, portfolio_6x8);
        lv_style_set_text_color(&studio_7, lv_color_hex(0x222222));

        lv_style_init(&studio_8);
        lv_style_set_text_font(&studio_8, fmtowns_8x16);
        lv_style_set_text_color(&studio_8, lv_color_hex(0xffffff));

        lv_style_init(&studio_9);
        lv_style_set_text_font(&studio_9, portfolio_6x8);
        lv_style_set_text_color(&studio_9, lv_color_hex(0x222222));

        lv_style_init(&studio_10);
        lv_style_set_text_font(&studio_10, fmtowns_8x16);
        lv_style_set_text_color(&studio_10, lv_color_hex(0xffffff));

        lv_style_init(&studio_11);
        lv_style_set_text_font(&studio_11, portfolio_6x8);
        lv_style_set_text_color(&studio_11, lv_color_hex(0x222222));

        lv_style_init(&studio_12);
        lv_style_set_text_font(&studio_12, fmtowns_8x16);
        lv_style_set_text_color(&studio_12, lv_color_hex(0xffffff));

        lv_style_init(&studio_13);
        lv_style_set_text_font(&studio_13, portfolio_6x8);
        lv_style_set_text_color(&studio_13, lv_color_hex(0x222222));

        lv_style_init(&studio_14);
        lv_style_set_text_font(&studio_14, fmtowns_8x16);
        lv_style_set_text_color(&studio_14, lv_color_hex(0xffffff));

        lv_style_init(&studio_15);
        lv_style_set_text_font(&studio_15, portfolio_6x8);
        lv_style_set_text_color(&studio_15, lv_color_hex(0x222222));

        style_inited = true;
    }

    lv_obj_t * screen = lv_obj_create(NULL);
    lv_obj_set_name_static(screen, "main_screen_#");
    lv_obj_add_style(screen, &studio_screen, 0);

    lv_obj_t * layer_1 = lv_image_create(screen);
    lv_obj_set_x(layer_1, 2);
    lv_obj_set_y(layer_1, 2);
    lv_image_set_src(layer_1, studio_layer_1);

    lv_obj_t * layer_2 = lv_label_create(screen);
    lv_obj_set_x(layer_2, 7);
    lv_obj_set_y(layer_2, 32);
    lv_label_bind_text(layer_2, &device_identity, NULL);
    lv_obj_add_style(layer_2, &studio_1, 0);

    lv_obj_t * layer_3 = lv_label_create(screen);
    lv_obj_set_x(layer_3, 2);
    lv_obj_set_y(layer_3, 46);
    lv_label_bind_text(layer_3, &device_name_text, NULL);
    lv_obj_add_style(layer_3, &studio_2, 0);

    lv_obj_t * layer_4 = lv_label_create(screen);
    lv_obj_set_x(layer_4, 1);
    lv_obj_set_y(layer_4, 55);
    lv_label_bind_text(layer_4, &ip_address_text, NULL);
    lv_obj_add_style(layer_4, &studio_3, 0);

    lv_obj_t * layer_5 = lv_label_create(screen);
    lv_obj_set_x(layer_5, 106);
    lv_obj_set_y(layer_5, 1);
    lv_label_bind_text(layer_5, &hashrate_text, NULL);
    lv_obj_add_style(layer_5, &studio_4, 0);

    lv_obj_t * layer_6 = lv_label_create(screen);
    lv_obj_set_x(layer_6, 113);
    lv_obj_set_y(layer_6, 14);
    lv_label_set_text(layer_6, "HASHRATE");
    lv_obj_add_style(layer_6, &studio_5, 0);

    lv_obj_t * layer_7 = lv_label_create(screen);
    lv_obj_set_x(layer_7, 106);
    lv_obj_set_y(layer_7, 22);
    lv_label_bind_text(layer_7, &best_share_text, NULL);
    lv_obj_add_style(layer_7, &studio_6, 0);

    lv_obj_t * layer_8 = lv_label_create(screen);
    lv_obj_set_x(layer_8, 113);
    lv_obj_set_y(layer_8, 35);
    lv_label_set_text(layer_8, "BESTSHARE");
    lv_obj_add_style(layer_8, &studio_7, 0);

    lv_obj_t * layer_9 = lv_label_create(screen);
    lv_obj_set_x(layer_9, 106);
    lv_obj_set_y(layer_9, 43);
    lv_label_bind_text(layer_9, &temperature_text, NULL);
    lv_obj_add_style(layer_9, &studio_8, 0);

    lv_obj_t * layer_10 = lv_label_create(screen);
    lv_obj_set_x(layer_10, 113);
    lv_obj_set_y(layer_10, 56);
    lv_label_set_text(layer_10, "TEMP");
    lv_obj_add_style(layer_10, &studio_9, 0);

    lv_obj_t * layer_11 = lv_label_create(screen);
    lv_obj_set_x(layer_11, 191);
    lv_obj_set_y(layer_11, 1);
    lv_label_bind_text(layer_11, &power_text, NULL);
    lv_obj_add_style(layer_11, &studio_10, 0);

    lv_obj_t * layer_12 = lv_label_create(screen);
    lv_obj_set_x(layer_12, 199);
    lv_obj_set_y(layer_12, 14);
    lv_label_set_text(layer_12, "POWER");
    lv_obj_add_style(layer_12, &studio_11, 0);

    lv_obj_t * layer_13 = lv_label_create(screen);
    lv_obj_set_x(layer_13, 191);
    lv_obj_set_y(layer_13, 22);
    lv_label_bind_text(layer_13, &frequency_text, NULL);
    lv_obj_add_style(layer_13, &studio_12, 0);

    lv_obj_t * layer_14 = lv_label_create(screen);
    lv_obj_set_x(layer_14, 199);
    lv_obj_set_y(layer_14, 35);
    lv_label_set_text(layer_14, "FREQUENCY");
    lv_obj_add_style(layer_14, &studio_13, 0);

    lv_obj_t * layer_15 = lv_label_create(screen);
    lv_obj_set_x(layer_15, 191);
    lv_obj_set_y(layer_15, 43);
    lv_label_bind_text(layer_15, &fan_text, NULL);
    lv_obj_add_style(layer_15, &studio_14, 0);

    lv_obj_t * layer_16 = lv_label_create(screen);
    lv_obj_set_x(layer_16, 199);
    lv_obj_set_y(layer_16, 56);
    lv_label_set_text(layer_16, "FAN");
    lv_obj_add_style(layer_16, &studio_15, 0);

    LV_TRACE_OBJ_CREATE("finished");
    return screen;
}
