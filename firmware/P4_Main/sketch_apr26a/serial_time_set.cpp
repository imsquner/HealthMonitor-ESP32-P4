#include "serial_time_set.h"

#include <Arduino.h>
#include <string.h>

#include "c6_network.h"
#include "ui_heart_test.h"

static constexpr size_t SERIAL_LINE_MAX = 256;

static char s_line[SERIAL_LINE_MAX];
static size_t s_line_len = 0;
static bool s_line_discarding = false;

static void reset_line(void)
{
    s_line_len = 0;
    s_line_discarding = false;
    s_line[0] = '\0';
}

static const char *skip_spaces(const char *text)
{
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

static bool is_command_boundary(char ch)
{
    return ch == '\0' || ch == ' ' || ch == '\t';
}

static bool starts_with_ignore_case(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        char left = *text++;
        char right = *prefix++;

        if (left >= 'a' && left <= 'z') {
            left = (char)(left - 'a' + 'A');
        }
        if (right >= 'a' && right <= 'z') {
            right = (char)(right - 'a' + 'A');
        }

        if (left != right) {
            return false;
        }
    }

    return true;
}

static const char *get_ai_question(const char *line)
{
    const char *command = skip_spaces(line);

    if (starts_with_ignore_case(command, "AI ASK") && is_command_boundary(command[6])) {
        return skip_spaces(command + 6);
    }

    if (starts_with_ignore_case(command, "AIASK") && is_command_boundary(command[5])) {
        return skip_spaces(command + 5);
    }

    if (starts_with_ignore_case(command, "ASK") && is_command_boundary(command[3])) {
        return skip_spaces(command + 3);
    }

    return command;
}

static void handle_line(const char *line)
{
    const char *command = skip_spaces(line);
    if (starts_with_ignore_case(command, "CLOUD ON") && is_command_boundary(command[8])) {
        c6_network_set_cloud_sync_enabled(true);
        Serial.println("Cloud sync ON forwarded to C6");
        return;
    }
    if (starts_with_ignore_case(command, "CLOUD OFF") && is_command_boundary(command[9])) {
        c6_network_set_cloud_sync_enabled(false);
        Serial.println("Cloud sync OFF forwarded to C6");
        return;
    }
    if (starts_with_ignore_case(command, "TEST START") && is_command_boundary(command[10])) {
        ui_heart_test_debug_start();
        return;
    }
    if (starts_with_ignore_case(command, "TEST STOP") && is_command_boundary(command[9])) {
        ui_heart_test_debug_stop();
        return;
    }

    const char *question = get_ai_question(line);
    if (question[0] == '\0') {
        Serial.println("AI question is empty");
        return;
    }

    c6_network_send_ai_question(question);
    Serial.println("AI question forwarded to C6");
}

void serial_time_set_poll(void)
{
    while (Serial.available() > 0) {
        const char ch = (char)Serial.read();

        if ((ch == '\r') || (ch == '\n')) {
            if (s_line_discarding) {
                reset_line();
            } else if (s_line_len > 0) {
                s_line[s_line_len] = '\0';
                handle_line(s_line);
                reset_line();
            }
            continue;
        }

        if (s_line_len < (sizeof(s_line) - 1)) {
            s_line[s_line_len++] = ch;
        } else {
            s_line_len = 0;
            s_line[0] = '\0';
            s_line_discarding = true;
            Serial.println("Serial command too long, max 255 bytes");
        }
    }
}
