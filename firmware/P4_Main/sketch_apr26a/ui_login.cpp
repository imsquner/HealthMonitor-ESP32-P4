#include "ui_login.h"

#include <Arduino.h>
#include <string.h>

#include "ui/ui.h"
#include "ui_screen_nav.h"

LV_IMG_DECLARE(ui_img_qr_wechat);
LV_FONT_DECLARE(ui_font_login_40);
LV_FONT_DECLARE(ui_font_login_28);

namespace {
const char *LOGIN_USER = "admin";
const char *LOGIN_PASSWORD = "123456";

bool authenticated = false;
lv_obj_t *login_screen = nullptr;
lv_obj_t *user_textarea = nullptr;
lv_obj_t *password_textarea = nullptr;
lv_obj_t *keyboard = nullptr;
lv_obj_t *status_label = nullptr;

void set_obj_bg(lv_obj_t *obj, uint32_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void set_label_style(lv_obj_t *label, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y, int w,
                     const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_style(label, font, color);
    return label;
}

void style_textarea(lv_obj_t *textarea)
{
    lv_obj_set_size(textarea, 380, 58);
    lv_obj_clear_flag(textarea, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_bg(textarea, 0xFFFFFF, 255);
    lv_obj_set_style_radius(textarea, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(textarea, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(textarea, lv_color_hex(0x111111), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(textarea, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(textarea, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(textarea, lv_color_hex(0x111111), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_textarea_set_one_line(textarea, true);
}

void style_button(lv_obj_t *button)
{
    lv_obj_set_size(button, 150, 82);
    set_obj_bg(button, 0xFFFFFF, 255);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(0x111111), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void style_skip_button(lv_obj_t *button)
{
    lv_obj_set_size(button, 190, 92);
    set_obj_bg(button, 0x92D050, 255);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(0x111111), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void set_status(const char *text, uint32_t color)
{
    if (status_label == nullptr) {
        return;
    }
    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

void hide_keyboard(void)
{
    if (keyboard == nullptr) {
        return;
    }
    lv_keyboard_set_textarea(keyboard, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_FOCUSED && code != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *textarea = lv_event_get_target(event);
    if (keyboard != nullptr) {
        lv_keyboard_set_textarea(keyboard, textarea);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(keyboard);
    }
}

void keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_keyboard();
    }
}

void enter_main_screen(void)
{
    authenticated = true;
    hide_keyboard();
    set_status("", 0x1F9D45);
    ui_screen_nav_load(0);
}

void login_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *user = lv_textarea_get_text(user_textarea);
    const char *password = lv_textarea_get_text(password_textarea);
    if (strcmp(user, LOGIN_USER) == 0 && strcmp(password, LOGIN_PASSWORD) == 0) {
        enter_main_screen();
        return;
    }

    lv_textarea_set_text(password_textarea, "");
    set_status("用户名或密码错误", 0xC00000);
}

void register_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    set_status("请联系管理员注册", 0x1667B7);
}

void skip_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    enter_main_screen();
}

void create_login_screen(void)
{
    login_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(login_screen, LV_OBJ_FLAG_SCROLLABLE);
    set_obj_bg(login_screen, 0xFFFFFF, 255);

    make_label(login_screen, "健康守护系统登陆", 0, 60, 1280, &ui_font_login_40, 0x000000, LV_TEXT_ALIGN_CENTER);

    make_label(login_screen, "用户名：", 155, 210, 190, &ui_font_login_40, 0x000000, LV_TEXT_ALIGN_LEFT);
    user_textarea = lv_textarea_create(login_screen);
    style_textarea(user_textarea);
    lv_obj_set_pos(user_textarea, 350, 204);
    lv_textarea_set_max_length(user_textarea, 24);
    lv_textarea_set_accepted_chars(user_textarea, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.@");
    lv_textarea_set_placeholder_text(user_textarea, "admin");
    lv_obj_add_event_cb(user_textarea, textarea_event_cb, LV_EVENT_ALL, nullptr);

    make_label(login_screen, "密码：", 155, 310, 190, &ui_font_login_40, 0x000000, LV_TEXT_ALIGN_LEFT);
    password_textarea = lv_textarea_create(login_screen);
    style_textarea(password_textarea);
    lv_obj_set_pos(password_textarea, 350, 304);
    lv_textarea_set_max_length(password_textarea, 24);
    lv_textarea_set_password_mode(password_textarea, true);
    lv_textarea_set_placeholder_text(password_textarea, "123456");
    lv_obj_add_event_cb(password_textarea, textarea_event_cb, LV_EVENT_ALL, nullptr);

    lv_obj_t *register_button = lv_btn_create(login_screen);
    style_button(register_button);
    lv_obj_set_pos(register_button, 250, 420);
    lv_obj_t *register_label = lv_label_create(register_button);
    lv_label_set_text(register_label, "注册");
    set_label_style(register_label, &ui_font_login_40, 0x111111);
    lv_obj_center(register_label);
    lv_obj_add_event_cb(register_button, register_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *login_button = lv_btn_create(login_screen);
    style_button(login_button);
    lv_obj_set_pos(login_button, 520, 420);
    lv_obj_t *login_label = lv_label_create(login_button);
    lv_label_set_text(login_label, "登陆");
    set_label_style(login_label, &ui_font_login_40, 0x111111);
    lv_obj_center(login_label);
    lv_obj_add_event_cb(login_button, login_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *qr = lv_img_create(login_screen);
    lv_img_set_src(qr, &ui_img_qr_wechat);
    lv_obj_set_pos(qr, 830, 200);

    make_label(login_screen, "请扫描二维码加入企业微信", 155, 575, 700, &ui_font_login_40, 0x000000, LV_TEXT_ALIGN_LEFT);
    status_label = make_label(login_screen, "", 155, 515, 620, &ui_font_login_40, 0xC00000, LV_TEXT_ALIGN_LEFT);

    make_label(login_screen, "若您想直接体验请点击下方按钮", 750, 535, 500, &ui_font_login_28, 0x000000,
               LV_TEXT_ALIGN_CENTER);
    lv_obj_t *skip_button = lv_btn_create(login_screen);
    style_skip_button(skip_button);
    lv_obj_set_pos(skip_button, 915, 586);
    lv_obj_t *skip_label = lv_label_create(skip_button);
    lv_label_set_text(skip_label, "跳过\n直接登陆");
    set_label_style(skip_label, &ui_font_login_28, 0x111111);
    lv_obj_set_style_text_align(skip_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(skip_label);
    lv_obj_add_event_cb(skip_button, skip_event_cb, LV_EVENT_CLICKED, nullptr);

    keyboard = lv_keyboard_create(login_screen);
    lv_obj_set_size(keyboard, 900, 200);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, nullptr);
}
}

bool ui_login_is_authenticated(void)
{
    return authenticated;
}

void ui_login_show(void)
{
    if (authenticated) {
        return;
    }
    if (login_screen == nullptr) {
        create_login_screen();
    }
    hide_keyboard();
    if (user_textarea != nullptr) {
        lv_textarea_set_text(user_textarea, "");
    }
    if (password_textarea != nullptr) {
        lv_textarea_set_text(password_textarea, "");
    }
    set_status("", 0xC00000);
    lv_scr_load(login_screen);
}

void ui_login_logout(void)
{
    authenticated = false;
    ui_login_show();
}
