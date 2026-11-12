#include "ui_heart_test.h"

#include <Arduino.h>
#include <time.h>

#include "HeartRateCore.h"
#include "SpO2Core.h"
#include "c6_network.h"
#include "lvgl_mipilcd_port.h"
#include "p4_vital_sensor.h"
#include "ui/ui.h"
#include "ui_status_bar.h"

static const uint16_t CHART_POINT_COUNT = 80;
static const uint16_t SESSION_MAX_POINTS = 240;
static const uint8_t HISTORY_MAX_RECORDS = 10;
static const uint16_t HISTORY_CHART_VISIBLE_POINTS = 20;
static const uint32_t SAMPLE_INTERVAL_MS = 10;
static const int32_t DIRECT_SENSOR_FINGER_THRESHOLD = 15000;
static const uint32_t HEART_WAVE_UPDATE_INTERVAL_MS = 50;
static const uint32_t VALUE_UPDATE_INTERVAL_MS = 250;
static const uint32_t SESSION_RECORD_INTERVAL_MS = 1000;
static const lv_coord_t HEART_WAVE_DISPLAY_MIN = 8;
static const lv_coord_t HEART_WAVE_DISPLAY_MAX = 92;
static const lv_coord_t HEART_WAVE_MIN_RAW_SPAN = 2500;
static const lv_coord_t HEART_WAVE_RAW_PADDING = 600;
static const lv_coord_t HEART_WAVE_RAW_LIMIT = 24000;
static const lv_coord_t HISTORY_PANEL_WIDTH = 194;
static const lv_coord_t HISTORY_PANEL_HEIGHT = 44;
static const lv_coord_t HISTORY_PANEL_GAP = 5;
static const lv_coord_t HISTORY_PANEL_TOP = 52;
static const lv_coord_t HISTORY_SPO2_MIN = 85;
static const lv_coord_t HISTORY_SPO2_MAX = 100;
static const lv_coord_t HISTORY_HEART_MIN = 0;
static const lv_coord_t HISTORY_HEART_MAX = 160;
static const lv_coord_t HISTORY_HEART_DANGER_LOW = 50;
static const lv_coord_t HISTORY_HEART_WARNING_LOW = 60;
static const lv_coord_t HISTORY_HEART_WARNING_HIGH = 100;
static const lv_coord_t HISTORY_HEART_DANGER_HIGH = 120;
static const lv_coord_t EXERCISE_HEART_WARNING_HIGH = 120;
static const lv_coord_t EXERCISE_HEART_DANGER_HIGH = 145;
static const lv_coord_t HISTORY_SPO2_DANGER_HIGH = 90;
static const lv_coord_t HISTORY_SPO2_WARNING_HIGH = 95;
static const lv_coord_t BLOOD_CHART_DISPLAY_MIN = 850;
static const lv_coord_t BLOOD_CHART_DISPLAY_MAX = 1000;
static const lv_color_t STATUS_COLOR_NORMAL = lv_color_hex(0x00A86B);
static const lv_color_t STATUS_COLOR_WARNING = lv_color_hex(0xD6A600);
static const lv_color_t STATUS_COLOR_DANGER = lv_color_hex(0xD32F2F);
static const lv_color_t STATUS_COLOR_EMPTY = lv_color_hex(0x6C7A84);
static const lv_color_t HISTORY_COLOR_SELECTED = lv_color_hex(0x2F80ED);
static const lv_color_t HISTORY_COLOR_VALID = lv_color_hex(0xDFE7EE);
static const lv_color_t HISTORY_COLOR_EMPTY = lv_color_hex(0xE8EEF2);
static const lv_color_t HISTORY_CHART_HEART_COLOR = lv_color_hex(0x2176FF);
static const lv_color_t HISTORY_CHART_SPO2_COLOR = lv_color_hex(0xD32F2F);
static const lv_color_t HISTORY_CHART_THRESHOLD_COLOR = lv_color_hex(0xD6A600);
static const lv_color_t HISTORY_CHART_DANGER_BG_COLOR = lv_color_hex(0xFF3B30);
static const lv_color_t HISTORY_TITLE_DISABLED_COLOR = lv_color_hex(0x8E8E8E);

enum DiagnosisLevel {
    DIAGNOSIS_NORMAL,
    DIAGNOSIS_WARNING,
    DIAGNOSIS_DANGER,
};

enum ActivityMode {
    ACTIVITY_MODE_REST,
    ACTIVITY_MODE_AFTER_EXERCISE,
};

struct VitalThresholds {
    int16_t heartDangerLow;
    int16_t heartWarningLow;
    int16_t heartWarningHigh;
    int16_t heartDangerHigh;
    int16_t spo2Danger;
    int16_t spo2Warning;
};

struct VitalPoint {
    int16_t heartRate;
    int16_t spo2;
};

struct VitalStats {
    int16_t maxHeart;
    int16_t maxSpo2;
    int16_t minHeart;
    int16_t minSpo2;
    int16_t avgHeart;
    int16_t avgSpo2;
    int16_t medianHeart;
    int16_t medianSpo2;
    uint16_t dangerHeart;
    uint16_t dangerSpo2;
    float varianceHeart;
    float varianceSpo2;
    DiagnosisLevel diagnosis;
};

struct HistoryRecord {
    bool valid;
    ActivityMode activityMode;
    time_t startTime;
    time_t endTime;
    uint16_t pointCount;
    VitalPoint points[SESSION_MAX_POINTS];
    VitalStats stats;
};

struct RawVitalSample {
    int32_t redValue;
    int32_t irValue;
    uint32_t sampleTimeMs;
    bool fingerPresent;
};

static RawVitalSample s_latest_sample = {};
static float s_latest_heart_rate = 0.0f;
static int s_latest_spo2 = -1;
static float s_latest_spo2_plot = -1.0f;

static bool s_danger_alert_active = false;
static bool s_collecting = false;
static uint32_t s_last_sample_ms = 0;
static uint32_t s_last_heart_wave_update_ms = 0;
static uint32_t s_last_value_update_ms = 0;
static uint32_t s_last_session_record_ms = 0;

static lv_chart_series_t *s_heart_series = nullptr;
static lv_chart_series_t *s_blood_series = nullptr;
static lv_chart_series_t *s_history_spo2_series = nullptr;
static lv_chart_series_t *s_history_heart_series = nullptr;
static lv_coord_t s_heart_points[CHART_POINT_COUNT];
static lv_coord_t s_blood_points[CHART_POINT_COUNT];
static lv_coord_t s_heart_wave_raw_points[CHART_POINT_COUNT];
static lv_coord_t s_history_spo2_points[SESSION_MAX_POINTS];
static lv_coord_t s_history_heart_points[SESSION_MAX_POINTS];
static lv_coord_t s_latest_heart_wave = LV_CHART_POINT_NONE;
static bool s_heart_wave_baseline_ready = false;
static float s_heart_wave_baseline = 0.0f;
static time_t s_session_start_time = 0;
static uint16_t s_session_point_count = 0;
static VitalPoint s_session_points[SESSION_MAX_POINTS];
static HistoryRecord s_history[HISTORY_MAX_RECORDS];
static lv_obj_t *s_history_panels[HISTORY_MAX_RECORDS];
static lv_obj_t *s_history_labels[HISTORY_MAX_RECORDS];
static lv_obj_t *s_history_heart_toggle_label = nullptr;
static lv_obj_t *s_history_spo2_toggle_label = nullptr;
static lv_obj_t *s_history_diagnosis_label = nullptr;
static int8_t s_selected_history = -1;
static bool s_history_heart_visible = true;
static bool s_history_spo2_visible = true;
static bool s_history_callbacks_attached = false;
static bool s_history_chart_event_attached = false;
static bool s_history_toggle_callbacks_attached = false;
static bool s_history_ai_callback_attached = false;
static uint16_t s_today_record_count = 0;
static int16_t s_last_home_heart = -1;
static int16_t s_last_home_spo2 = -1;
static ActivityMode s_activity_mode = ACTIVITY_MODE_REST;
static ActivityMode s_session_activity_mode = ACTIVITY_MODE_REST;
static ActivityMode s_last_home_activity_mode = ACTIVITY_MODE_REST;
static lv_obj_t *s_activity_panel = nullptr;
static lv_obj_t *s_activity_rest_button = nullptr;
static lv_obj_t *s_activity_rest_label = nullptr;
static lv_obj_t *s_activity_exercise_button = nullptr;
static lv_obj_t *s_activity_exercise_label = nullptr;

extern HardwareSerial S3Serial;
static char s_s3_line_buf[64] = "";
static size_t s_s3_line_idx = 0;

static const char *diagnosis_text(DiagnosisLevel level);
static lv_color_t diagnosis_color(DiagnosisLevel level);
static lv_color_t heart_rate_color_for_mode(int heart_rate, ActivityMode mode);
static lv_color_t heart_rate_color(int heart_rate);
static lv_color_t spo2_color(int spo2);
static void update_home_latest_values(void);

static VitalThresholds thresholds_for_mode(ActivityMode mode)
{
    VitalThresholds thresholds = {
        HISTORY_HEART_DANGER_LOW,
        HISTORY_HEART_WARNING_LOW,
        HISTORY_HEART_WARNING_HIGH,
        HISTORY_HEART_DANGER_HIGH,
        HISTORY_SPO2_DANGER_HIGH,
        HISTORY_SPO2_WARNING_HIGH
    };

    if (mode == ACTIVITY_MODE_AFTER_EXERCISE) {
        thresholds.heartWarningHigh = EXERCISE_HEART_WARNING_HIGH;
        thresholds.heartDangerHigh = EXERCISE_HEART_DANGER_HIGH;
    }

    return thresholds;
}

static const char *activity_mode_text(ActivityMode mode)
{
    return mode == ACTIVITY_MODE_AFTER_EXERCISE ? "运动后" : "静息";
}

static bool activity_mode_is_after_exercise(ActivityMode mode)
{
    return mode == ACTIVITY_MODE_AFTER_EXERCISE;
}

static ActivityMode history_chart_threshold_mode(void)
{
    if (s_selected_history >= 0 &&
        s_selected_history < HISTORY_MAX_RECORDS &&
        s_history[s_selected_history].valid) {
        return s_history[s_selected_history].activityMode;
    }

    return s_activity_mode;
}

static void fill_points(lv_coord_t *points, lv_coord_t value)
{
    for (uint16_t i = 0; i < CHART_POINT_COUNT; i++) {
        points[i] = value;
    }
}

static void fill_history_points(lv_coord_t *points, lv_coord_t value)
{
    for (uint16_t i = 0; i < SESSION_MAX_POINTS; i++) {
        points[i] = value;
    }
}

static lv_chart_series_t *get_or_add_series(lv_obj_t *chart, lv_color_t color)
{
    if (chart == nullptr) {
        return nullptr;
    }

    lv_chart_series_t *series = lv_chart_get_series_next(chart, nullptr);
    if (series == nullptr) {
        series = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
    } else {
        lv_chart_set_series_color(chart, series, color);
    }
    return series;
}

static lv_chart_series_t *get_or_add_series_at(lv_obj_t *chart, uint8_t index,
                                                lv_color_t color, lv_chart_axis_t axis)
{
    if (chart == nullptr) {
        return nullptr;
    }

    lv_chart_series_t *series = lv_chart_get_series_next(chart, nullptr);
    for (uint8_t i = 0; series != nullptr && i < index; i++) {
        series = lv_chart_get_series_next(chart, series);
    }

    if (series == nullptr) {
        series = lv_chart_add_series(chart, color, axis);
    } else {
        lv_chart_set_series_color(chart, series, color);
    }
    return series;
}

static void setup_chart(lv_obj_t *chart, lv_chart_series_t **series, lv_coord_t *points,
                        lv_color_t color, lv_coord_t min_value, lv_coord_t max_value)
{
    if (chart == nullptr) {
        *series = nullptr;
        return;
    }

    fill_points(points, LV_CHART_POINT_NONE);
    *series = get_or_add_series(chart, color);
    if (*series == nullptr) {
        return;
    }

    lv_chart_set_ext_y_array(chart, *series, points);
    lv_chart_set_point_count(chart, CHART_POINT_COUNT);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, min_value, max_value);
    lv_chart_set_all_value(chart, *series, LV_CHART_POINT_NONE);
    lv_chart_refresh(chart);
}

static void setup_charts(void)
{
    fill_points(s_heart_wave_raw_points, LV_CHART_POINT_NONE);
    s_latest_heart_wave = LV_CHART_POINT_NONE;
    s_heart_wave_baseline_ready = false;
    s_heart_wave_baseline = 0.0f;
    setup_chart(ui_Chartheart, &s_heart_series, s_heart_points, lv_color_hex(0xD33A3A), 0, 100);
    setup_chart(ui_Chartblood, &s_blood_series, s_blood_points, lv_color_hex(0x2176FF),
                BLOOD_CHART_DISPLAY_MIN, BLOOD_CHART_DISPLAY_MAX);
    if (ui_Chartblood != nullptr) {
        lv_chart_set_axis_tick(ui_Chartblood, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 3, 2, false, 0);
    }
}

static lv_coord_t clamp_coord(lv_coord_t value, lv_coord_t low, lv_coord_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static lv_coord_t map_history_heart_y(lv_obj_t *chart, lv_coord_t value)
{
    lv_area_t area;
    lv_obj_get_content_coords(chart, &area);
    value = clamp_coord(value, HISTORY_HEART_MIN, HISTORY_HEART_MAX);

    int32_t height = lv_area_get_height(&area) - 1;
    int32_t span = HISTORY_HEART_MAX - HISTORY_HEART_MIN;
    int32_t y_offset = ((int32_t)(value - HISTORY_HEART_MIN) * height) / span;
    return area.y2 - (lv_coord_t)y_offset;
}

static lv_coord_t map_history_spo2_y(lv_obj_t *chart, lv_coord_t value)
{
    lv_area_t area;
    lv_obj_get_content_coords(chart, &area);
    value = clamp_coord(value, HISTORY_SPO2_MIN, HISTORY_SPO2_MAX);

    int32_t height = lv_area_get_height(&area) - 1;
    int32_t span = HISTORY_SPO2_MAX - HISTORY_SPO2_MIN;
    int32_t y_offset = ((int32_t)(value - HISTORY_SPO2_MIN) * height) / span;
    return area.y2 - (lv_coord_t)y_offset;
}

static bool history_chart_has_visible_series(void)
{
    return s_history_heart_visible || s_history_spo2_visible;
}

static bool history_chart_should_draw_heart_guides(void)
{
    return s_history_heart_visible;
}

static bool history_chart_should_draw_spo2_guides(void)
{
    return !s_history_heart_visible && s_history_spo2_visible;
}

static void draw_history_heart_danger_background(lv_obj_t *chart, lv_draw_ctx_t *draw_ctx)
{
    lv_area_t area;
    lv_obj_get_content_coords(chart, &area);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = HISTORY_CHART_DANGER_BG_COLOR;
    rect_dsc.bg_opa = LV_OPA_30;
    rect_dsc.border_opa = LV_OPA_TRANSP;
    rect_dsc.radius = 0;

    VitalThresholds thresholds = thresholds_for_mode(history_chart_threshold_mode());
    lv_coord_t y_high = map_history_heart_y(chart, thresholds.heartDangerHigh);
    lv_coord_t y_low = map_history_heart_y(chart, thresholds.heartDangerLow);

    lv_area_t high_area = { area.x1, area.y1, area.x2, y_high };
    if (high_area.y1 <= high_area.y2) {
        lv_draw_rect(draw_ctx, &rect_dsc, &high_area);
    }

    lv_area_t low_area = { area.x1, y_low, area.x2, area.y2 };
    if (low_area.y1 <= low_area.y2) {
        lv_draw_rect(draw_ctx, &rect_dsc, &low_area);
    }
}

static void draw_history_spo2_danger_background(lv_obj_t *chart, lv_draw_ctx_t *draw_ctx)
{
    lv_area_t area;
    lv_obj_get_content_coords(chart, &area);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = HISTORY_CHART_DANGER_BG_COLOR;
    rect_dsc.bg_opa = LV_OPA_30;
    rect_dsc.border_opa = LV_OPA_TRANSP;
    rect_dsc.radius = 0;

    lv_coord_t y_danger = map_history_spo2_y(chart, HISTORY_SPO2_DANGER_HIGH);
    lv_area_t danger_area = { area.x1, y_danger, area.x2, area.y2 };
    if (danger_area.y1 <= danger_area.y2) {
        lv_draw_rect(draw_ctx, &rect_dsc, &danger_area);
    }
}

static void draw_history_threshold_line(lv_obj_t *chart, lv_draw_ctx_t *draw_ctx,
                                        lv_coord_t y)
{
    lv_area_t area;
    lv_obj_get_content_coords(chart, &area);

    lv_point_t p1 = { area.x1, y };
    lv_point_t p2 = { area.x2, p1.y };

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = HISTORY_CHART_THRESHOLD_COLOR;
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.width = 2;
    lv_draw_line(draw_ctx, &line_dsc, &p1, &p2);
}

static void history_chart_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *chart = lv_event_get_target(event);

    if (code == LV_EVENT_DRAW_PART_BEGIN) {
        lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(event);
        if (lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_DIV_LINE_INIT)) {
            if (history_chart_should_draw_heart_guides()) {
                draw_history_heart_danger_background(chart, dsc->draw_ctx);
            } else if (history_chart_should_draw_spo2_guides()) {
                draw_history_spo2_danger_background(chart, dsc->draw_ctx);
            }
        }
        return;
    }

    if (code == LV_EVENT_DRAW_POST_END) {
        if (!history_chart_has_visible_series()) {
            return;
        }

        lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(event);
        if (history_chart_should_draw_heart_guides()) {
            VitalThresholds thresholds = thresholds_for_mode(history_chart_threshold_mode());
            draw_history_threshold_line(chart, draw_ctx, map_history_heart_y(chart, thresholds.heartWarningLow));
            draw_history_threshold_line(chart, draw_ctx, map_history_heart_y(chart, thresholds.heartWarningHigh));
        } else if (history_chart_should_draw_spo2_guides()) {
            draw_history_threshold_line(chart, draw_ctx, map_history_spo2_y(chart, HISTORY_SPO2_WARNING_HIGH));
        }
    }
}

static void apply_history_chart_visibility(void)
{
    if (ui_Charthisshow == nullptr || s_history_spo2_series == nullptr || s_history_heart_series == nullptr) {
        return;
    }

    lv_chart_hide_series(ui_Charthisshow, s_history_heart_series, !s_history_heart_visible);
    lv_chart_hide_series(ui_Charthisshow, s_history_spo2_series, !s_history_spo2_visible);
    lv_chart_refresh(ui_Charthisshow);
}

static void update_history_toggle_label_styles(void)
{
    if (s_history_heart_toggle_label != nullptr) {
        lv_obj_set_style_text_color(s_history_heart_toggle_label,
                                    s_history_heart_visible ? HISTORY_CHART_HEART_COLOR : HISTORY_TITLE_DISABLED_COLOR,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_history_spo2_toggle_label != nullptr) {
        lv_obj_set_style_text_color(s_history_spo2_toggle_label,
                                    s_history_spo2_visible ? HISTORY_CHART_SPO2_COLOR : HISTORY_TITLE_DISABLED_COLOR,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void set_history_diagnosis_placeholder(void)
{
    if (s_history_diagnosis_label == nullptr) {
        return;
    }

    lv_label_set_text(s_history_diagnosis_label, "本次健康情况--");
    lv_obj_set_style_text_color(s_history_diagnosis_label, STATUS_COLOR_EMPTY, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void show_history_diagnosis(DiagnosisLevel level)
{
    if (s_history_diagnosis_label == nullptr) {
        return;
    }

    lv_label_set_text(s_history_diagnosis_label, diagnosis_text(level));
    lv_obj_set_style_text_color(s_history_diagnosis_label, diagnosis_color(level), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void setup_history_chart(void)
{
    if (ui_Charthisshow == nullptr) {
        return;
    }

    fill_history_points(s_history_spo2_points, LV_CHART_POINT_NONE);
    fill_history_points(s_history_heart_points, LV_CHART_POINT_NONE);

    s_history_spo2_series = get_or_add_series_at(ui_Charthisshow, 0, HISTORY_CHART_SPO2_COLOR, LV_CHART_AXIS_SECONDARY_Y);
    s_history_heart_series = get_or_add_series_at(ui_Charthisshow, 1, HISTORY_CHART_HEART_COLOR, LV_CHART_AXIS_PRIMARY_Y);
    if (s_history_spo2_series == nullptr || s_history_heart_series == nullptr) {
        return;
    }

    lv_chart_set_ext_y_array(ui_Charthisshow, s_history_spo2_series, s_history_spo2_points);
    lv_chart_set_ext_y_array(ui_Charthisshow, s_history_heart_series, s_history_heart_points);
    lv_chart_set_point_count(ui_Charthisshow, HISTORY_CHART_VISIBLE_POINTS);
    lv_chart_set_type(ui_Charthisshow, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(ui_Charthisshow, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_range(ui_Charthisshow, LV_CHART_AXIS_PRIMARY_Y, HISTORY_HEART_MIN, HISTORY_HEART_MAX);
    lv_chart_set_range(ui_Charthisshow, LV_CHART_AXIS_SECONDARY_Y, HISTORY_SPO2_MIN, HISTORY_SPO2_MAX);
    lv_chart_set_zoom_x(ui_Charthisshow, 256);
    lv_chart_set_zoom_y(ui_Charthisshow, 256);
    lv_chart_set_x_start_point(ui_Charthisshow, s_history_spo2_series, 0);
    lv_chart_set_x_start_point(ui_Charthisshow, s_history_heart_series, 0);
    lv_obj_add_flag(ui_Charthisshow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_Charthisshow, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ui_Charthisshow, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(ui_Charthisshow, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(ui_Charthisshow, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(ui_Charthisshow, 3, LV_PART_ITEMS);

    if (!s_history_chart_event_attached) {
        lv_obj_add_event_cb(ui_Charthisshow, history_chart_event_cb, LV_EVENT_ALL, nullptr);
        s_history_chart_event_attached = true;
    }

    apply_history_chart_visibility();
    lv_chart_refresh(ui_Charthisshow);
}

static void clear_history_chart(void)
{
    if (ui_Charthisshow == nullptr || s_history_spo2_series == nullptr || s_history_heart_series == nullptr) {
        return;
    }

    fill_history_points(s_history_spo2_points, LV_CHART_POINT_NONE);
    fill_history_points(s_history_heart_points, LV_CHART_POINT_NONE);
    lv_chart_set_point_count(ui_Charthisshow, HISTORY_CHART_VISIBLE_POINTS);
    lv_chart_set_zoom_x(ui_Charthisshow, 256);
    lv_chart_set_x_start_point(ui_Charthisshow, s_history_spo2_series, 0);
    lv_chart_set_x_start_point(ui_Charthisshow, s_history_heart_series, 0);
    lv_obj_scroll_to_x(ui_Charthisshow, 0, LV_ANIM_OFF);
    apply_history_chart_visibility();
    lv_chart_refresh(ui_Charthisshow);
}

static void render_history_chart(const HistoryRecord &record)
{
    if (ui_Charthisshow == nullptr || s_history_spo2_series == nullptr || s_history_heart_series == nullptr) {
        return;
    }

    uint16_t point_count = record.pointCount;
    if (point_count > SESSION_MAX_POINTS) {
        point_count = SESSION_MAX_POINTS;
    }
    uint16_t chart_point_count = point_count < 2 ? 2 : point_count;

    fill_history_points(s_history_spo2_points, LV_CHART_POINT_NONE);
    fill_history_points(s_history_heart_points, LV_CHART_POINT_NONE);
    for (uint16_t i = 0; i < point_count; i++) {
        s_history_heart_points[i] = record.points[i].heartRate;
        s_history_spo2_points[i] = record.points[i].spo2;
    }

    lv_chart_set_ext_y_array(ui_Charthisshow, s_history_spo2_series, s_history_spo2_points);
    lv_chart_set_ext_y_array(ui_Charthisshow, s_history_heart_series, s_history_heart_points);
    lv_chart_set_point_count(ui_Charthisshow, chart_point_count);
    lv_chart_set_x_start_point(ui_Charthisshow, s_history_spo2_series, 0);
    lv_chart_set_x_start_point(ui_Charthisshow, s_history_heart_series, 0);

    uint32_t zoom_x = 256;
    if (chart_point_count > HISTORY_CHART_VISIBLE_POINTS) {
        zoom_x = (uint32_t)256 * (chart_point_count - 1) / (HISTORY_CHART_VISIBLE_POINTS - 1);
        if (zoom_x < 256) {
            zoom_x = 256;
        }
    }
    if (zoom_x > 4096) {
        zoom_x = 4096;
    }
    lv_chart_set_zoom_x(ui_Charthisshow, (uint16_t)zoom_x);
    lv_obj_scroll_to_x(ui_Charthisshow, 0, LV_ANIM_OFF);
    apply_history_chart_visibility();
    lv_chart_refresh(ui_Charthisshow);
}

static void shift_append(lv_coord_t *points, lv_coord_t value)
{
    for (uint16_t i = 1; i < CHART_POINT_COUNT; i++) {
        points[i - 1] = points[i];
    }
    points[CHART_POINT_COUNT - 1] = value;
}

static bool get_point_bounds(const lv_coord_t *points, lv_coord_t *min_value, lv_coord_t *max_value)
{
    bool has_value = false;

    for (uint16_t i = 0; i < CHART_POINT_COUNT; i++) {
        lv_coord_t value = points[i];
        if (value == LV_CHART_POINT_NONE) {
            continue;
        }

        if (!has_value) {
            *min_value = value;
            *max_value = value;
            has_value = true;
        } else {
            if (value < *min_value) {
                *min_value = value;
            }
            if (value > *max_value) {
                *max_value = value;
            }
        }
    }

    return has_value;
}

static lv_coord_t map_heart_wave_value(lv_coord_t raw_value, lv_coord_t raw_min, lv_coord_t raw_max)
{
    lv_coord_t span = raw_max - raw_min;
    if (span < HEART_WAVE_MIN_RAW_SPAN) {
        lv_coord_t center = (raw_min + raw_max) / 2;
        raw_min = center - HEART_WAVE_MIN_RAW_SPAN / 2;
        raw_max = raw_min + HEART_WAVE_MIN_RAW_SPAN;
        span = raw_max - raw_min;
    }

    int32_t display_span = HEART_WAVE_DISPLAY_MAX - HEART_WAVE_DISPLAY_MIN;
    int32_t mapped = HEART_WAVE_DISPLAY_MIN +
                     ((int32_t)(raw_value - raw_min) * display_span) / span;
    return clamp_coord((lv_coord_t)mapped, HEART_WAVE_DISPLAY_MIN, HEART_WAVE_DISPLAY_MAX);
}

static void remap_heart_wave_points(void)
{
    lv_coord_t raw_min = 0;
    lv_coord_t raw_max = 0;
    if (!get_point_bounds(s_heart_wave_raw_points, &raw_min, &raw_max)) {
        fill_points(s_heart_points, LV_CHART_POINT_NONE);
        return;
    }

    raw_min -= HEART_WAVE_RAW_PADDING;
    raw_max += HEART_WAVE_RAW_PADDING;

    for (uint16_t i = 0; i < CHART_POINT_COUNT; i++) {
        lv_coord_t raw_value = s_heart_wave_raw_points[i];
        s_heart_points[i] = (raw_value == LV_CHART_POINT_NONE) ? LV_CHART_POINT_NONE :
                            map_heart_wave_value(raw_value, raw_min, raw_max);
    }
}

static void update_heart_wave_from_sample(void)
{
    if (!s_latest_sample.fingerPresent) {
        s_heart_wave_baseline_ready = false;
        s_latest_heart_wave = LV_CHART_POINT_NONE;
        return;
    }

    float ir_value = (float)s_latest_sample.irValue;
    if (!s_heart_wave_baseline_ready) {
        s_heart_wave_baseline = ir_value;
        s_heart_wave_baseline_ready = true;
        s_latest_heart_wave = 0;
        return;
    }

    s_heart_wave_baseline = 0.985f * s_heart_wave_baseline + 0.015f * ir_value;
    lv_coord_t wave_value = (lv_coord_t)(ir_value - s_heart_wave_baseline);
    s_latest_heart_wave = clamp_coord(wave_value, -HEART_WAVE_RAW_LIMIT, HEART_WAVE_RAW_LIMIT);
}

static void update_heart_wave_chart(void)
{
    if (ui_Chartheart == nullptr || s_heart_series == nullptr) {
        return;
    }

    shift_append(s_heart_wave_raw_points, s_latest_heart_wave);
    remap_heart_wave_points();
    lv_chart_set_x_start_point(ui_Chartheart, s_heart_series, 0);
    lv_chart_refresh(ui_Chartheart);
}

static void reset_vital_processing(void)
{
    s_latest_sample = {};
    s_latest_heart_rate = 0.0f;
    s_latest_spo2 = -1;
    s_latest_spo2_plot = -1.0f;
    HeartRateCore_reset();
    SpO2Core_reset();
}

static void process_raw_vital_sample(bool finger_present, int32_t red_value,
                                     int32_t ir_value, uint32_t sample_time_ms)
{
    s_latest_sample.fingerPresent = finger_present;
    s_latest_sample.redValue = red_value;
    s_latest_sample.irValue = ir_value;
    s_latest_sample.sampleTimeMs = sample_time_ms;

    if (!finger_present) {
        s_latest_heart_rate = 0.0f;
        s_latest_spo2 = -1;
        s_latest_spo2_plot = -1.0f;
        HeartRateCore_reset();
        SpO2Core_reset();
        return;
    }

    HeartRateCore_update(ir_value, sample_time_ms);
    if (HeartRateCore_hasNewValue()) {
        if (HeartRateCore_isValid()) {
            s_latest_heart_rate = HeartRateCore_getValue();
        }
        HeartRateCore_clearNewValueFlag();
    }

    SpO2Core_update(red_value, ir_value, sample_time_ms);
    if (SpO2Core_hasNewValue()) {
        if (SpO2Core_isValid()) {
            s_latest_spo2 = (int)(SpO2Core_getValue() + 0.5f);
            s_latest_spo2_plot = SpO2Core_getPlotValue();
        }
        SpO2Core_clearNewValueFlag();
    }
}

static void process_legacy_vital_sample(bool finger_present, int32_t ir_value,
                                        int heart_rate, int spo2, uint32_t receive_time_ms)
{
    s_latest_sample.fingerPresent = finger_present;
    s_latest_sample.redValue = 0;
    s_latest_sample.irValue = ir_value;
    s_latest_sample.sampleTimeMs = receive_time_ms;
    if (finger_present) {
        s_latest_heart_rate = (float)heart_rate;
        s_latest_spo2 = spo2;
        s_latest_spo2_plot = (float)spo2;
    } else {
        s_latest_heart_rate = 0.0f;
        s_latest_spo2 = -1;
        s_latest_spo2_plot = -1.0f;
        HeartRateCore_reset();
        SpO2Core_reset();
    }
}

static bool poll_p4_vital_sensor(void)
{
    bool had_sample = false;
    long red = 0;
    long ir = 0;
    uint32_t sample_time_ms = 0;

    while (p4_vital_sensor_read_next_sample(red, ir, sample_time_ms)) {
        had_sample = true;
        process_raw_vital_sample(ir >= DIRECT_SENSOR_FINGER_THRESHOLD,
                                 (int32_t)red,
                                 (int32_t)ir,
                                 sample_time_ms);
    }

    return had_sample || p4_vital_sensor_is_available();
}

static void poll_s3_vital_serial(void)
{
    while (S3Serial.available() > 0) {
        char c = S3Serial.read();
        if (c == '\n') {
            s_s3_line_buf[s_s3_line_idx] = '\0';
            int finger = 0;
            long red = 0;
            long ir = 0;
            unsigned long sample_time_ms = 0;
            int hr = 0;
            int spo2 = 0;
            uint32_t receive_time_ms = millis();
            if (sscanf(s_s3_line_buf, "R %d %ld %ld %lu", &finger, &red, &ir, &sample_time_ms) == 4) {
                process_raw_vital_sample(finger != 0, (int32_t)red, (int32_t)ir,
                                         sample_time_ms == 0 ? receive_time_ms : (uint32_t)sample_time_ms);
            } else if (sscanf(s_s3_line_buf, "S %d %ld %d %d", &finger, &ir, &hr, &spo2) == 4) {
                process_legacy_vital_sample(finger != 0, (int32_t)ir, hr, spo2, receive_time_ms);
            }
            s_s3_line_idx = 0;
        } else if (s_s3_line_idx < sizeof(s_s3_line_buf) - 1) {
            s_s3_line_buf[s_s3_line_idx++] = c;
        }
    }
}

static void drain_s3_vital_serial(void)
{
    while (S3Serial.available() > 0) {
        S3Serial.read();
    }
    s_s3_line_idx = 0;
}

static int current_spo2(uint32_t now_ms, const RawVitalSample &sample)
{
    (void)now_ms;
    if (!sample.fingerPresent) {
        return -1;
    }
    return s_latest_spo2;
}

static lv_coord_t current_spo2_plot_value(int fallback_spo2)
{
    float plot_value = s_latest_spo2_plot >= 0.0f ? s_latest_spo2_plot : (float)fallback_spo2;
    lv_coord_t chart_value = (lv_coord_t)(plot_value * 10.0f + 0.5f);
    return clamp_coord(chart_value, BLOOD_CHART_DISPLAY_MIN, BLOOD_CHART_DISPLAY_MAX);
}

static ActivityMode home_threshold_mode(void)
{
    return s_collecting ? s_activity_mode : s_last_home_activity_mode;
}

static void set_label(lv_obj_t *label, const char *text)
{
    if (label != nullptr) {
        lv_label_set_text(label, text);
    }
}

static void set_label_color(lv_obj_t *label, lv_color_t color)
{
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void style_activity_button(lv_obj_t *button, lv_obj_t *label, bool selected)
{
    if (button == nullptr) {
        return;
    }

    lv_obj_set_style_bg_color(button, selected ? lv_color_hex(0x1F77D0) : lv_color_hex(0xFFFFFF),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, selected ? lv_color_hex(0x1F77D0) : lv_color_hex(0xB9C4CF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(button, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x1E2A38),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void update_activity_button_styles(void)
{
    style_activity_button(s_activity_rest_button, s_activity_rest_label,
                          s_activity_mode == ACTIVITY_MODE_REST);
    style_activity_button(s_activity_exercise_button, s_activity_exercise_label,
                          s_activity_mode == ACTIVITY_MODE_AFTER_EXERCISE);
}

static void activity_mode_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    ActivityMode mode = (ActivityMode)(uintptr_t)lv_event_get_user_data(event);
    s_activity_mode = mode;
    if (s_collecting) {
        s_session_activity_mode = mode;
        s_last_home_activity_mode = mode;
    }

    update_activity_button_styles();
    if (s_latest_sample.fingerPresent && s_latest_heart_rate > 0.0f) {
        int heart_rate = (int)(s_latest_heart_rate + 0.5f);
        set_label_color(ui_Labelnum1, heart_rate_color_for_mode(heart_rate, s_activity_mode));
    }
    update_home_latest_values();
}

static lv_obj_t *create_activity_mode_button(lv_obj_t *parent, const char *text,
                                             lv_coord_t x, lv_coord_t width,
                                             ActivityMode mode,
                                             lv_obj_t **label_out)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_size(button, width, 62);
    lv_obj_set_pos(button, x, 68);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, activity_mode_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width - 4);
    lv_obj_set_style_text_font(label, &ui_font_cn_screen3_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    if (label_out != nullptr) {
        *label_out = label;
    }

    return button;
}

static void setup_activity_mode_ui(void)
{
    if (s_activity_panel != nullptr || ui_Panelscreen3 == nullptr) {
        update_activity_button_styles();
        return;
    }

    s_activity_panel = lv_obj_create(ui_Panelscreen3);
    lv_obj_set_size(s_activity_panel, 180, 154);
    lv_obj_set_pos(s_activity_panel, 1084, 454);
    lv_obj_clear_flag(s_activity_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_activity_panel, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_activity_panel, lv_color_hex(0xFBFCFE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_activity_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_activity_panel, lv_color_hex(0xD8DEE6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_activity_panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(s_activity_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_activity_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title = lv_label_create(s_activity_panel);
    lv_label_set_text(title, "您的状态");
    lv_obj_set_size(title, 180, 30);
    lv_obj_set_pos(title, 0, 22);
    lv_obj_set_style_text_font(title, &ui_font_cn_screen3_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0x1E2A38), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_activity_rest_button = create_activity_mode_button(s_activity_panel, "静息", 12, 62,
                                                         ACTIVITY_MODE_REST, &s_activity_rest_label);
    s_activity_exercise_button = create_activity_mode_button(s_activity_panel, "运动后", 82, 88,
                                                             ACTIVITY_MODE_AFTER_EXERCISE,
                                                             &s_activity_exercise_label);
    update_activity_button_styles();
}

static void update_home_status_label(const char *text, lv_color_t color)
{
    set_label(ui_LabelhomeStatus, text);
    set_label_color(ui_LabelhomeStatus, color);
}

static void update_home_count_label(void)
{
    char text[12];
    snprintf(text, sizeof(text), "%u", (unsigned)s_today_record_count);
    set_label(ui_LabelhomeCount, text);
}

static void update_home_latest_values(void)
{
    char text[16];
    if (s_last_home_heart > 0) {
        snprintf(text, sizeof(text), "%d", s_last_home_heart);
        set_label(ui_LabelhomeHeart, text);
        set_label_color(ui_LabelhomeHeart,
                        heart_rate_color_for_mode(s_last_home_heart, home_threshold_mode()));
    } else {
        set_label(ui_LabelhomeHeart, "--");
        set_label_color(ui_LabelhomeHeart, STATUS_COLOR_EMPTY);
    }

    if (s_last_home_spo2 > 0) {
        snprintf(text, sizeof(text), "%d", s_last_home_spo2);
        set_label(ui_LabelhomeSpo2, text);
        set_label_color(ui_LabelhomeSpo2, spo2_color(s_last_home_spo2));
    } else {
        set_label(ui_LabelhomeSpo2, "--");
        set_label_color(ui_LabelhomeSpo2, STATUS_COLOR_EMPTY);
    }
}

static void update_home_diagnosis_label(DiagnosisLevel level)
{
    set_label(ui_LabelhomeDiagnosis, diagnosis_text(level));
    set_label_color(ui_LabelhomeDiagnosis, diagnosis_color(level));
}

static void init_home_labels(void)
{
    update_home_status_label(s_collecting ? "采集中" : "待检测",
                             s_collecting ? STATUS_COLOR_WARNING : STATUS_COLOR_EMPTY);
    update_home_count_label();
    update_home_latest_values();
    set_label(ui_LabelhomeDiagnosis, "--");
    set_label_color(ui_LabelhomeDiagnosis, STATUS_COLOR_EMPTY);
}

static void update_status_label(const char *text)
{
    set_label(ui_Labelsuccess, text);
}

static lv_color_t heart_rate_color_for_mode(int heart_rate, ActivityMode mode)
{
    VitalThresholds thresholds = thresholds_for_mode(mode);
    if (heart_rate < thresholds.heartDangerLow || heart_rate > thresholds.heartDangerHigh) {
        return STATUS_COLOR_DANGER;
    }
    if (heart_rate < thresholds.heartWarningLow || heart_rate > thresholds.heartWarningHigh) {
        return STATUS_COLOR_WARNING;
    }
    return STATUS_COLOR_NORMAL;
}

static lv_color_t heart_rate_color(int heart_rate)
{
    return heart_rate_color_for_mode(heart_rate, s_activity_mode);
}

static lv_color_t spo2_color(int spo2)
{
    if (spo2 <= 90) {
        return STATUS_COLOR_DANGER;
    }
    if (spo2 <= 94) {
        return STATUS_COLOR_WARNING;
    }
    return STATUS_COLOR_NORMAL;
}

static bool is_heart_danger(int heart_rate, ActivityMode mode)
{
    VitalThresholds thresholds = thresholds_for_mode(mode);
    return heart_rate < thresholds.heartDangerLow || heart_rate > thresholds.heartDangerHigh;
}

static bool is_heart_warning(int heart_rate, ActivityMode mode)
{
    VitalThresholds thresholds = thresholds_for_mode(mode);
    return heart_rate < thresholds.heartWarningLow || heart_rate > thresholds.heartWarningHigh;
}

static bool is_spo2_danger(int spo2)
{
    return spo2 <= 90;
}

static bool is_spo2_warning(int spo2)
{
    return spo2 <= 94;
}

static DiagnosisLevel evaluate_diagnosis(const VitalPoint *points, uint16_t count,
                                         const VitalStats &stats, ActivityMode mode)
{
    if (count == 0) {
        return DIAGNOSIS_WARNING;
    }

    uint16_t warning_count = 0;
    uint16_t danger_count = 0;
    uint16_t warning_streak = 0;
    uint16_t danger_streak = 0;
    uint16_t max_warning_streak = 0;
    uint16_t max_danger_streak = 0;

    for (uint16_t i = 0; i < count; i++) {
        bool danger = is_heart_danger(points[i].heartRate, mode) || is_spo2_danger(points[i].spo2);
        bool warning = is_heart_warning(points[i].heartRate, mode) || is_spo2_warning(points[i].spo2);

        if (danger) {
            danger_count++;
            danger_streak++;
            warning_streak++;
        } else if (warning) {
            warning_count++;
            warning_streak++;
            danger_streak = 0;
        } else {
            warning_streak = 0;
            danger_streak = 0;
        }

        if (warning_streak > max_warning_streak) {
            max_warning_streak = warning_streak;
        }
        if (danger_streak > max_danger_streak) {
            max_danger_streak = danger_streak;
        }
    }

    uint16_t warning_ratio = (uint16_t)((uint32_t)warning_count * 100U / count);
    uint16_t danger_ratio = (uint16_t)((uint32_t)danger_count * 100U / count);

    bool danger_level = danger_count >= 2 ||
                        danger_ratio > 5 ||
                        max_danger_streak >= 2 ||
                        warning_ratio > 35 ||
                        max_warning_streak > 8 ||
                        stats.varianceHeart > 324.0f ||
                        stats.varianceSpo2 > 16.0f ||
                        stats.avgSpo2 < 95 ||
                        stats.minSpo2 <= thresholds_for_mode(mode).spo2Danger;
    if (danger_level) {
        return DIAGNOSIS_DANGER;
    }

    bool warning_level = danger_count > 0 ||
                         warning_ratio > 15 ||
                         max_warning_streak > 3 ||
                         stats.varianceHeart > 100.0f ||
                         stats.varianceSpo2 > 4.0f;
    if (warning_level) {
        return DIAGNOSIS_WARNING;
    }

    return DIAGNOSIS_NORMAL;
}

static const char *diagnosis_text(DiagnosisLevel level)
{
    switch (level) {
        case DIAGNOSIS_NORMAL:
            return "正常";
        case DIAGNOSIS_WARNING:
            return "数据波动";
        case DIAGNOSIS_DANGER:
        default:
            return "异常";
    }
}

static lv_color_t diagnosis_color(DiagnosisLevel level)
{
    switch (level) {
        case DIAGNOSIS_NORMAL:
            return STATUS_COLOR_NORMAL;
        case DIAGNOSIS_WARNING:
            return STATUS_COLOR_WARNING;
        case DIAGNOSIS_DANGER:
        default:
            return STATUS_COLOR_DANGER;
    }
}

static uint32_t diagnosis_color_hex(DiagnosisLevel level)
{
    switch (level) {
        case DIAGNOSIS_NORMAL:
            return 0x00A86B;
        case DIAGNOSIS_WARNING:
            return 0xD6A600;
        case DIAGNOSIS_DANGER:
        default:
            return 0xD32F2F;
    }
}

static void sort_values(int16_t *values, uint16_t count)
{
    for (uint16_t i = 1; i < count; i++) {
        int16_t key = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > key) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = key;
    }
}

static int16_t median_value(int16_t *values, uint16_t count)
{
    if (count == 0) {
        return 0;
    }

    sort_values(values, count);
    if ((count & 1) != 0) {
        return values[count / 2];
    }
    return (int16_t)((values[count / 2 - 1] + values[count / 2] + 1) / 2);
}

static void calculate_stats(const VitalPoint *points, uint16_t count,
                            VitalStats *stats, ActivityMode mode)
{
    if (count == 0) {
        *stats = {};
        return;
    }

    int32_t heart_sum = 0;
    int32_t spo2_sum = 0;
    static int16_t heart_values[SESSION_MAX_POINTS];
    static int16_t spo2_values[SESSION_MAX_POINTS];

    stats->maxHeart = points[0].heartRate;
    stats->minHeart = points[0].heartRate;
    stats->maxSpo2 = points[0].spo2;
    stats->minSpo2 = points[0].spo2;
    stats->dangerHeart = 0;
    stats->dangerSpo2 = 0;

    for (uint16_t i = 0; i < count; i++) {
        int16_t heart_rate = points[i].heartRate;
        int16_t spo2 = points[i].spo2;

        if (heart_rate > stats->maxHeart) {
            stats->maxHeart = heart_rate;
        }
        if (heart_rate < stats->minHeart) {
            stats->minHeart = heart_rate;
        }
        if (spo2 > stats->maxSpo2) {
            stats->maxSpo2 = spo2;
        }
        if (spo2 < stats->minSpo2) {
            stats->minSpo2 = spo2;
        }

        if (is_heart_danger(heart_rate, mode)) {
            stats->dangerHeart++;
        }
        if (is_spo2_danger(spo2)) {
            stats->dangerSpo2++;
        }

        heart_sum += heart_rate;
        spo2_sum += spo2;
        heart_values[i] = heart_rate;
        spo2_values[i] = spo2;
    }

    float heart_avg = (float)heart_sum / (float)count;
    float spo2_avg = (float)spo2_sum / (float)count;
    float heart_variance_sum = 0.0f;
    float spo2_variance_sum = 0.0f;

    for (uint16_t i = 0; i < count; i++) {
        float heart_delta = (float)points[i].heartRate - heart_avg;
        float spo2_delta = (float)points[i].spo2 - spo2_avg;
        heart_variance_sum += heart_delta * heart_delta;
        spo2_variance_sum += spo2_delta * spo2_delta;
    }

    stats->avgHeart = (int16_t)(heart_avg + 0.5f);
    stats->avgSpo2 = (int16_t)(spo2_avg + 0.5f);
    stats->medianHeart = median_value(heart_values, count);
    stats->medianSpo2 = median_value(spo2_values, count);
    stats->varianceHeart = heart_variance_sum / (float)count;
    stats->varianceSpo2 = spo2_variance_sum / (float)count;
    stats->diagnosis = evaluate_diagnosis(points, count, *stats, mode);
}

static void format_history_time(time_t unix_time, char *buffer, size_t size)
{
    struct tm local_tm = {};
    localtime_r(&unix_time, &local_tm);
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d",
             local_tm.tm_year + 1900,
             local_tm.tm_mon + 1,
             local_tm.tm_mday,
             local_tm.tm_hour,
             local_tm.tm_min);
}

static void set_screen4_placeholder(void)
{
    set_label(ui_Label_highest, "最高  -- | --");
    set_label(ui_Label_lowest, "最低  -- | --");
    set_label(ui_Label_ave, "平均  -- | --");
    set_label(ui_Label_middle, "中位  -- | --");
    set_label(ui_Label_dangernum, "危险  -- | --");
    set_label(ui_Label_judge, "方差  -- | --");
    set_history_diagnosis_placeholder();
    clear_history_chart();
}

static void display_history_record(uint8_t index)
{
    if (index >= HISTORY_MAX_RECORDS || !s_history[index].valid) {
        s_selected_history = -1;
        set_screen4_placeholder();
        return;
    }

    const VitalStats &stats = s_history[index].stats;
    char buffer[64];

    snprintf(buffer, sizeof(buffer), "最高  %d | %d", stats.maxHeart, stats.maxSpo2);
    set_label(ui_Label_highest, buffer);
    snprintf(buffer, sizeof(buffer), "最低  %d | %d", stats.minHeart, stats.minSpo2);
    set_label(ui_Label_lowest, buffer);
    snprintf(buffer, sizeof(buffer), "平均  %d | %d", stats.avgHeart, stats.avgSpo2);
    set_label(ui_Label_ave, buffer);
    snprintf(buffer, sizeof(buffer), "中位  %d | %d", stats.medianHeart, stats.medianSpo2);
    set_label(ui_Label_middle, buffer);
    snprintf(buffer, sizeof(buffer), "危险  %u | %u", stats.dangerHeart, stats.dangerSpo2);
    set_label(ui_Label_dangernum, buffer);
    snprintf(buffer, sizeof(buffer), "方差  %.1f | %.1f", stats.varianceHeart, stats.varianceSpo2);
    set_label(ui_Label_judge, buffer);
    show_history_diagnosis(stats.diagnosis);

    s_selected_history = (int8_t)index;
    s_history_heart_visible = true;
    s_history_spo2_visible = true;
    update_history_toggle_label_styles();
    render_history_chart(s_history[index]);
}

static void update_history_panel_styles(void)
{
    for (uint8_t i = 0; i < HISTORY_MAX_RECORDS; i++) {
        if (s_history_panels[i] == nullptr) {
            continue;
        }

        lv_color_t color = s_history[i].valid ? HISTORY_COLOR_VALID : HISTORY_COLOR_EMPTY;
        lv_coord_t width = s_history[i].valid ? 2 : 1;
        if (s_selected_history == (int8_t)i) {
            color = HISTORY_COLOR_SELECTED;
            width = 4;
        }

        lv_obj_set_style_border_color(s_history_panels[i], color, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_history_panels[i], width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(s_history_panels[i],
                                  s_selected_history == (int8_t)i ? lv_color_hex(0xD8ECFF) : lv_color_hex(0xFFFFFF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void refresh_history_list(void)
{
    for (uint8_t i = 0; i < HISTORY_MAX_RECORDS; i++) {
        if (s_history_labels[i] == nullptr) {
            continue;
        }

        if (!s_history[i].valid) {
            set_label(s_history_labels[i], "");
            continue;
        }

        char history_time[24];
        format_history_time(s_history[i].startTime, history_time, sizeof(history_time));
        set_label(s_history_labels[i], history_time);
    }

    update_history_panel_styles();
}

static void insert_history_record(void)
{
    if (s_session_point_count == 0) {
        return;
    }

    for (int i = HISTORY_MAX_RECORDS - 1; i > 0; i--) {
        s_history[i] = s_history[i - 1];
    }

    HistoryRecord &record = s_history[0];
    record.valid = true;
    record.activityMode = s_session_activity_mode;
    record.startTime = s_session_start_time;
    record.endTime = time(nullptr);
    record.pointCount = s_session_point_count;
    for (uint16_t i = 0; i < s_session_point_count; i++) {
        record.points[i] = s_session_points[i];
    }
    calculate_stats(record.points, record.pointCount, &record.stats, record.activityMode);
    if (record.stats.diagnosis == DIAGNOSIS_DANGER) {
        s_danger_alert_active = true;
    }
    ui_status_bar_set_local_judgement_text(diagnosis_text(record.stats.diagnosis),
                                           diagnosis_color_hex(record.stats.diagnosis));
    s_today_record_count++;
    s_last_home_heart = record.stats.avgHeart;
    s_last_home_spo2 = record.stats.avgSpo2;
    s_last_home_activity_mode = record.activityMode;
    update_home_count_label();
    update_home_latest_values();
    update_home_diagnosis_label(record.stats.diagnosis);

    display_history_record(0);
    refresh_history_list();
}

static void append_history_ai_summary(char *buffer, size_t size)
{
    if (size == 0) {
        return;
    }

    size_t len = strlen(buffer);
    if (len + 1 >= size) {
        return;
    }

    len += snprintf(buffer + len, size - len, "历史:");
    for (uint8_t i = 0; i < HISTORY_MAX_RECORDS && len + 1 < size; i++) {
        if (!s_history[i].valid) {
            continue;
        }
        const VitalStats &s = s_history[i].stats;
        len += snprintf(buffer + len, size - len,
                        "#%u心%d-%d均%d氧%d-%d均%d判%s;",
                        (unsigned)(i + 1),
                        s.minHeart,
                        s.maxHeart,
                        s.avgHeart,
                        s.minSpo2,
                        s.maxSpo2,
                        s.avgSpo2,
                        diagnosis_text(s.diagnosis));
    }
    buffer[size - 1] = '\0';
}

static void request_ai_for_record(const HistoryRecord &record)
{
    const VitalStats &stats = record.stats;
    int detect_hour = -1;
    if (record.endTime > 0) {
        struct tm tm_info;
        if (localtime_r(&record.endTime, &tm_info) != nullptr) {
            detect_hour = tm_info.tm_hour;
        }
    }
    c6_network_set_latest_ai_summary(stats.maxHeart,
                                     stats.minHeart,
                                     stats.avgHeart,
                                     stats.maxSpo2,
                                     stats.minSpo2,
                                     stats.avgSpo2,
                                     detect_hour,
                                     activity_mode_is_after_exercise(record.activityMode),
                                     diagnosis_text(stats.diagnosis));
    char question[1800];
    snprintf(question, sizeof(question),
             "你是智能检测心率血氧系统的健康风险评估助手。请根据一次检测数据和历史记录生成面向老人屏幕与监护人微信的专业建议，只能输出指定格式。"
             "ELDER写给屏幕端，70字内，四句用分号分隔，结构为综合评估;异常重点;处置建议;升级条件。避免生僻字，不能写诊断病名。"
             "CHILD写给子女和监护人，220字内，结构为本次概览、历史趋势、风险解释、下一步建议。"
             "CHILD必须包含心率最高最低、血氧最低；若有历史字段，必须比较系统保存的全部历史记录，说明是否反复波动、低氧反复、心率极值反复或整体稳定。"
             "建议要像产品化健康报告：语气专业、具体、可执行；可提示静息复测、佩戴校准、记录症状、必要时联系专业人员；不得夸大为确诊。"
             "LEVEL只能填normal、warn或danger；本地判断异常时LEVEL必须danger；本地判断数据波动时LEVEL不能normal。"
             "全部中文只用GB2312一级常用简体字和常用标点，禁用繁体、生僻字、emoji、特殊符号。"
             "除固定格式字段AI、ELDER、CHILD、LEVEL、normal、warn、danger外，不要英文单词，不要解释格式，不要输出药物剂量。"
             "格式：@@|AI|ELDER:...|CHILD:...|LEVEL:...|##。"
             "数据:状态%s;心率均值%d最高%d最低%d;血氧均值%d最高%d最低%d;危险点心率%u血氧%u;本地判断:%s;检测小时%d。",
             activity_mode_text(record.activityMode),
             stats.avgHeart,
             stats.maxHeart,
             stats.minHeart,
             stats.avgSpo2,
             stats.maxSpo2,
             stats.minSpo2,
             (unsigned)stats.dangerHeart,
             (unsigned)stats.dangerSpo2,
             diagnosis_text(stats.diagnosis),
             detect_hour);
    append_history_ai_summary(question, sizeof(question));
    c6_network_send_ai_question(question);
}

static void history_ai_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_selected_history < 0 || s_selected_history >= HISTORY_MAX_RECORDS) {
        return;
    }

    uint8_t index = (uint8_t)s_selected_history;
    if (!s_history[index].valid) {
        return;
    }

    request_ai_for_record(s_history[index]);
}

static void record_session_value(uint32_t now_ms)
{
    if (s_session_point_count >= SESSION_MAX_POINTS) {
        return;
    }

    if (!s_latest_sample.fingerPresent || s_latest_heart_rate <= 0.0f) {
        return;
    }

    int spo2 = current_spo2(now_ms, s_latest_sample);
    if (spo2 < 0) {
        return;
    }

    s_session_points[s_session_point_count].heartRate = (int16_t)(s_latest_heart_rate + 0.5f);
    s_session_points[s_session_point_count].spo2 = (int16_t)spo2;
    s_session_point_count++;
}

static void history_panel_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    display_history_record(index);
    update_history_panel_styles();
}

static void attach_history_event(lv_obj_t *obj, uint8_t index)
{
    if (obj == nullptr) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, history_panel_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
}

static void setup_screen4_stat_labels(void)
{
    lv_obj_t *labels[] = {
        ui_Label_highest,
        ui_Label_lowest,
        ui_Label_ave,
        ui_Label_middle,
        ui_Label_dangernum,
        ui_Label_judge,
    };
    const lv_coord_t y_positions[] = { 114, 158, 202, 246, 290, 334 };

    for (uint8_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
        if (labels[i] == nullptr) {
            continue;
        }

        lv_obj_set_width(labels[i], 218);
        lv_obj_set_align(labels[i], LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(labels[i], 20);
        lv_obj_set_y(labels[i], y_positions[i]);
        lv_obj_set_style_text_align(labels[i], LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(labels[i], &ui_font_cn_screen3_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void history_toggle_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    uint8_t target = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (target == 0) {
        s_history_heart_visible = !s_history_heart_visible;
    } else {
        s_history_spo2_visible = !s_history_spo2_visible;
    }

    update_history_toggle_label_styles();
    apply_history_chart_visibility();
}

static void setup_history_toggle_labels(void)
{
    if (ui_Label29 == nullptr) {
        return;
    }

    lv_obj_t *parent = lv_obj_get_parent(ui_Label29);
    s_history_heart_toggle_label = ui_Label29;
    if (s_history_spo2_toggle_label == nullptr) {
        s_history_spo2_toggle_label = lv_label_create(parent);
    }
    if (s_history_diagnosis_label == nullptr) {
        s_history_diagnosis_label = lv_label_create(parent);
    }

    lv_label_set_text(s_history_heart_toggle_label, "心率");
    lv_label_set_text(s_history_spo2_toggle_label, "血氧");

    lv_obj_t *labels[] = { s_history_heart_toggle_label, s_history_spo2_toggle_label };
    const lv_coord_t x_positions[] = { 82, 180 };
    for (uint8_t i = 0; i < 2; i++) {
        lv_obj_t *label = labels[i];
        if (label == nullptr) {
            continue;
        }

        lv_obj_set_width(label, 86);
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(label, x_positions[i]);
        lv_obj_set_y(label, 16);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(label, &ui_font_cn_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    update_history_toggle_label_styles();
    if (s_history_diagnosis_label != nullptr) {
        lv_obj_set_width(s_history_diagnosis_label, 340);
        lv_obj_set_height(s_history_diagnosis_label, LV_SIZE_CONTENT);
        lv_obj_set_align(s_history_diagnosis_label, LV_ALIGN_TOP_LEFT);
        lv_obj_set_x(s_history_diagnosis_label, 310);
        lv_obj_set_y(s_history_diagnosis_label, 18);
        lv_obj_set_style_text_font(s_history_diagnosis_label, &ui_font_cn_28, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(s_history_diagnosis_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(s_history_diagnosis_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        set_history_diagnosis_placeholder();
    }

    if (!s_history_toggle_callbacks_attached) {
        lv_obj_add_event_cb(s_history_heart_toggle_label, history_toggle_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
        lv_obj_add_event_cb(s_history_spo2_toggle_label, history_toggle_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)1);
        s_history_toggle_callbacks_attached = true;
    }
}

static void setup_history_slot_objects(void)
{
    if (ui_Panelhistory == nullptr) {
        return;
    }

    s_history_panels[0] = ui_Panel_HIS1;
    s_history_panels[1] = ui_Panel_HIS2;
    s_history_panels[2] = ui_Panel_HIS3;
    s_history_panels[3] = ui_Panel_HIS4;
    s_history_panels[4] = ui_Panel_HIS5;
    s_history_labels[0] = ui_Label24;
    s_history_labels[1] = ui_Label25;
    s_history_labels[2] = ui_Label26;
    s_history_labels[3] = ui_Label27;
    s_history_labels[4] = ui_Label28;

    lv_obj_add_flag(ui_Panelhistory, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_Panelhistory, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_Panelhistory, LV_SCROLLBAR_MODE_AUTO);

    for (uint8_t i = 5; i < HISTORY_MAX_RECORDS; i++) {
        if (s_history_panels[i] == nullptr) {
            s_history_panels[i] = lv_obj_create(ui_Panelhistory);
            s_history_labels[i] = lv_label_create(s_history_panels[i]);
        }
    }

    for (uint8_t i = 0; i < HISTORY_MAX_RECORDS; i++) {
        lv_obj_t *panel = s_history_panels[i];
        lv_obj_t *label = s_history_labels[i];
        if (panel == nullptr || label == nullptr) {
            continue;
        }

        lv_obj_set_width(panel, HISTORY_PANEL_WIDTH);
        lv_obj_set_height(panel, HISTORY_PANEL_HEIGHT);
        lv_obj_set_align(panel, LV_ALIGN_TOP_MID);
        lv_obj_set_x(panel, 0);
        lv_obj_set_y(panel, HISTORY_PANEL_TOP + (HISTORY_PANEL_HEIGHT + HISTORY_PANEL_GAP) * i);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(panel, HISTORY_COLOR_EMPTY, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_set_width(label, HISTORY_PANEL_WIDTH - 12);
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_obj_set_align(label, LV_ALIGN_CENTER);
        lv_obj_set_x(label, 0);
        lv_obj_set_y(label, 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &ui_font_Light, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0x202A33), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void setup_history_ui(void)
{
    setup_screen4_stat_labels();
    setup_history_toggle_labels();
    setup_history_slot_objects();
    setup_history_chart();
    set_screen4_placeholder();
    refresh_history_list();

    if (s_history_callbacks_attached) {
        return;
    }

    for (uint8_t i = 0; i < HISTORY_MAX_RECORDS; i++) {
        attach_history_event(s_history_panels[i], i);
        attach_history_event(s_history_labels[i], i);
    }
    if (!s_history_ai_callback_attached && ui_Imageaihelp != nullptr) {
        lv_obj_add_flag(ui_Imageaihelp, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ui_Imageaihelp, history_ai_event_cb, LV_EVENT_CLICKED, nullptr);
        s_history_ai_callback_attached = true;
    }
    s_history_callbacks_attached = true;
}

static void update_value_ui(uint32_t now_ms)
{
    lv_coord_t blood_value = LV_CHART_POINT_NONE;
    char text[16];

    if (s_latest_sample.fingerPresent && s_latest_heart_rate > 0.0f) {
        int heart_rate = (int)(s_latest_heart_rate + 0.5f);
        s_last_home_heart = (int16_t)heart_rate;
        s_last_home_activity_mode = s_activity_mode;
        snprintf(text, sizeof(text), "%d", heart_rate);
        set_label(ui_Labelnum1, text);
        set_label_color(ui_Labelnum1, heart_rate_color(heart_rate));
    } else {
        set_label(ui_Labelnum1, "--");
        set_label_color(ui_Labelnum1, STATUS_COLOR_EMPTY);
    }

    int spo2 = current_spo2(now_ms, s_latest_sample);
    if (spo2 >= 0) {
        blood_value = current_spo2_plot_value(spo2);
        s_last_home_spo2 = (int16_t)spo2;
        snprintf(text, sizeof(text), "%d%%", spo2);
        set_label(ui_Labelnum2, text);
        set_label_color(ui_Labelnum2, spo2_color(spo2));
    } else {
        set_label(ui_Labelnum2, "--");
        set_label_color(ui_Labelnum2, STATUS_COLOR_EMPTY);
    }

    if (ui_Chartblood != nullptr && s_blood_series != nullptr) {
        lv_chart_set_next_value(ui_Chartblood, s_blood_series, blood_value);
    }
    update_home_latest_values();
}

static void start_collection(void)
{
    s_collecting = true;
    s_danger_alert_active = false;
    s_last_sample_ms = millis();
    s_last_heart_wave_update_ms = 0;
    s_last_value_update_ms = 0;
    s_last_session_record_ms = s_last_sample_ms;
    s_session_start_time = time(nullptr);
    s_session_point_count = 0;
    s_session_activity_mode = s_activity_mode;
    s_last_home_activity_mode = s_activity_mode;
    s_s3_line_idx = 0;
    p4_vital_sensor_clear_fifo();
    drain_s3_vital_serial();

    reset_vital_processing();
    setup_charts();

    set_label(ui_Labelnum1, "--");
    set_label(ui_Labelnum2, "--");
    set_label_color(ui_Labelnum1, STATUS_COLOR_EMPTY);
    set_label_color(ui_Labelnum2, STATUS_COLOR_EMPTY);
    update_status_label("采集中");
    update_home_status_label("采集中", STATUS_COLOR_WARNING);
    update_home_latest_values();
    if (ui_Spinner2 != nullptr) {
        lv_obj_clear_flag(ui_Spinner2, LV_OBJ_FLAG_HIDDEN);
    }
}

static void stop_collection(void)
{
    s_collecting = false;
    insert_history_record();
    update_status_label("已停止");
    update_home_status_label("已完成", STATUS_COLOR_NORMAL);
    if (ui_Spinner2 != nullptr) {
        lv_obj_add_flag(ui_Spinner2, LV_OBJ_FLAG_HIDDEN);
    }
}

static void heart_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (lv_obj_has_state(ui_Button1, LV_STATE_CHECKED)) {
        start_collection();
    } else {
        stop_collection();
    }
}

void ui_heart_test_init(void)
{
    setup_charts();
    setup_history_ui();
    setup_activity_mode_ui();
    set_label(ui_Labelnum1, "--");
    set_label(ui_Labelnum2, "--");
    set_label_color(ui_Labelnum1, STATUS_COLOR_EMPTY);
    set_label_color(ui_Labelnum2, STATUS_COLOR_EMPTY);
    update_status_label("待检测");
    init_home_labels();

    if (ui_Button1 != nullptr) {
        lv_obj_add_event_cb(ui_Button1, heart_button_event_cb, LV_EVENT_CLICKED, nullptr);
    }
}

void ui_heart_test_debug_start(void)
{
    if (s_collecting) {
        Serial.println("[HEART TEST] already collecting");
        return;
    }

    if (lvgl_mipilcd_port_lock(100)) {
        if (ui_Button1 != nullptr) {
            lv_obj_add_state(ui_Button1, LV_STATE_CHECKED);
        }
        start_collection();
        lvgl_mipilcd_port_unlock();
        Serial.println("[HEART TEST] debug start");
    } else {
        Serial.println("[HEART TEST] debug start skipped: LVGL busy");
    }
}

void ui_heart_test_debug_stop(void)
{
    if (!s_collecting) {
        Serial.println("[HEART TEST] not collecting");
        return;
    }

    if (lvgl_mipilcd_port_lock(100)) {
        if (ui_Button1 != nullptr) {
            lv_obj_clear_state(ui_Button1, LV_STATE_CHECKED);
        }
        stop_collection();
        lvgl_mipilcd_port_unlock();
        Serial.printf("[HEART TEST] debug stop, points=%u\r\n", (unsigned)s_session_point_count);
    } else {
        Serial.println("[HEART TEST] debug stop skipped: LVGL busy");
    }
}

bool ui_heart_test_is_collecting(void)
{
    return s_collecting;
}

void ui_heart_test_debug_toggle(void)
{
    if (s_collecting) {
        ui_heart_test_debug_stop();
    } else {
        ui_heart_test_debug_start();
    }
}

bool ui_heart_test_has_danger_alert(void)
{
    return s_danger_alert_active;
}

void ui_heart_test_clear_danger_alert(void)
{
    s_danger_alert_active = false;
}

void ui_heart_test_poll(void)
{
    if (poll_p4_vital_sensor()) {
        drain_s3_vital_serial();
    } else {
        poll_s3_vital_serial();
    }

    if (!s_collecting) {
        return;
    }

    uint32_t now_ms = millis();
    update_heart_wave_from_sample();

    bool should_update_heart_wave = now_ms - s_last_heart_wave_update_ms >= HEART_WAVE_UPDATE_INTERVAL_MS;
    bool should_update_values = now_ms - s_last_value_update_ms >= VALUE_UPDATE_INTERVAL_MS;
    bool should_record_session = now_ms - s_last_session_record_ms >= SESSION_RECORD_INTERVAL_MS;
    if (should_record_session) {
        s_last_session_record_ms = now_ms;
        record_session_value(now_ms);
    }

    if (!should_update_heart_wave && !should_update_values) {
        return;
    }

    if (lvgl_mipilcd_port_lock(0)) {
        if (should_update_heart_wave) {
            s_last_heart_wave_update_ms = now_ms;
            update_heart_wave_chart();
        }
        if (should_update_values) {
            s_last_value_update_ms = now_ms;
            update_value_ui(now_ms);
        }
        lvgl_mipilcd_port_unlock();
    }
}
