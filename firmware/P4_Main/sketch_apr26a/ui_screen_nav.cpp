#include "ui_screen_nav.h"

#include "ui/ui.h"
#include "ui_login.h"

static const int PAGE_HOME = 0;
static const int PAGE_DETECT = 1;
static const int PAGE_HISTORY = 2;
static const int PAGE_SETTINGS = 3;
static const int PAGE_COUNT = 4;

static int normalize_page(int page)
{
    while (page < 0) {
        page += PAGE_COUNT;
    }
    while (page >= PAGE_COUNT) {
        page -= PAGE_COUNT;
    }
    return page;
}

void ui_screen_nav_load(int page)
{
    if (!ui_login_is_authenticated()) {
        ui_login_show();
        return;
    }

    page = normalize_page(page);
    switch (page) {
    case PAGE_DETECT:
        _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_FADE_ON, 260, 0, &ui_Screen3_screen_init);
        break;
    case PAGE_HISTORY:
        _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_FADE_ON, 260, 0, &ui_Screen4_screen_init);
        break;
    case PAGE_SETTINGS:
        _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 260, 0, &ui_Screen2_screen_init);
        break;
    case PAGE_HOME:
    default:
        _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 260, 0, &ui_Screen1_screen_init);
        break;
    }
}

static int active_page(void)
{
    lv_obj_t *active = lv_scr_act();
    if (active == ui_Screen3) return PAGE_DETECT;
    if (active == ui_Screen4) return PAGE_HISTORY;
    if (active == ui_Screen2) return PAGE_SETTINGS;
    return PAGE_HOME;
}

void ui_screen_nav_next(void)
{
    if (!ui_login_is_authenticated()) {
        ui_login_show();
        return;
    }
    ui_screen_nav_load(active_page() + 1);
}

void ui_screen_nav_prev(void)
{
    if (!ui_login_is_authenticated()) {
        ui_login_show();
        return;
    }
    ui_screen_nav_load(active_page() - 1);
}

void ui_screen_nav_load_settings(void)
{
    ui_screen_nav_load(PAGE_SETTINGS);
}

void ui_screen_nav_handle_gesture(lv_event_t *e, int current_page)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    if (!ui_login_is_authenticated()) {
        ui_login_show();
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir != LV_DIR_TOP && dir != LV_DIR_BOTTOM) {
        return;
    }

    lv_indev_wait_release(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        ui_screen_nav_load(current_page + 1);
    } else {
        ui_screen_nav_load(current_page - 1);
    }
}
