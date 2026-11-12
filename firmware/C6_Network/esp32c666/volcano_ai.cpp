#include "volcano_ai.h"

#include "secrets.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

static const char *API_KEY = ARK_API_KEY;
static const char *HOST = "ai-gateway.vei.volces.com";
static const int PORT = 443;
static const char *PATH = "/v1/chat/completions";
static const char *MODEL = "doubao-seed-1.6-flash";
static const int MAX_TOKENS = 300;
static const uint32_t HTTP_TIMEOUT_MS = 20000;

static bool api_key_valid()
{
    return API_KEY != nullptr && strncmp(API_KEY, "Bearer ", 7) == 0 && strlen(API_KEY) > 16;
}

static void set_error(String *error_message, const char *text)
{
    if (error_message != nullptr) {
        *error_message = text;
    }
}

static String trim_copy(String text)
{
    text.trim();
    return text;
}

static bool read_http_headers(WiFiClientSecure &client, int *status_code, bool *chunked, String *error_message)
{
    String status_line = client.readStringUntil('\n');
    status_line.trim();
    if (!status_line.startsWith("HTTP/")) {
        set_error(error_message, "AI HTTP STATUS MISSING");
        return false;
    }

    int first_space = status_line.indexOf(' ');
    if (first_space < 0) {
        set_error(error_message, "AI HTTP STATUS BAD");
        return false;
    }
    *status_code = status_line.substring(first_space + 1).toInt();
    *chunked = false;

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            return true;
        }

        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
            *chunked = true;
        }
    }

    return true;
}

static String read_plain_body(WiFiClientSecure &client)
{
    String body;
    uint32_t last_data_ms = millis();

    while (client.connected() || client.available() > 0) {
        while (client.available() > 0) {
            body += (char)client.read();
            last_data_ms = millis();
        }
        if (millis() - last_data_ms > 1000) {
            break;
        }
        delay(1);
    }

    return body;
}

static String read_chunked_body(WiFiClientSecure &client)
{
    String body;

    while (client.connected() || client.available() > 0) {
        String size_line = trim_copy(client.readStringUntil('\n'));
        if (size_line.length() == 0) {
            continue;
        }

        int semicolon = size_line.indexOf(';');
        if (semicolon >= 0) {
            size_line = size_line.substring(0, semicolon);
        }

        const int chunk_size = (int)strtol(size_line.c_str(), nullptr, 16);
        if (chunk_size <= 0) {
            break;
        }

        int remaining = chunk_size;
        while (remaining > 0) {
            if (client.available() <= 0) {
                delay(1);
                continue;
            }
            body += (char)client.read();
            remaining--;
        }

        client.readStringUntil('\n');
    }

    return body;
}

bool volcano_ai_is_configured()
{
    return api_key_valid();
}

String volcano_ai_ask(const String &question, String *error_message)
{
    if (!api_key_valid()) {
        set_error(error_message, "AI API KEY MISSING");
        return "";
    }

    if (WiFi.status() != WL_CONNECTED) {
        set_error(error_message, "AI WIFI NOT CONNECTED");
        return "";
    }

    JsonDocument request_doc;
    request_doc["model"] = MODEL;
    JsonArray messages = request_doc["messages"].to<JsonArray>();
    JsonObject user_message = messages.add<JsonObject>();
    user_message["role"] = "user";
    user_message["content"] = question;
    request_doc["max_tokens"] = MAX_TOKENS;

    // Use a temporary heap buffer to avoid the Arduino ESP32 Core 3.1.2 String = nullptr crash in ArduinoJson 7
    size_t json_len = measureJson(request_doc);
    char* post_buf = (char*)malloc(json_len + 1);
    if (post_buf == nullptr) {
        set_error(error_message, "AI OUT OF MEMORY");
        return "";
    }
    serializeJson(request_doc, post_buf, json_len + 1);
    String post_data(post_buf);
    free(post_buf);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS);

    if (!client.connect(HOST, PORT)) {
        set_error(error_message, "AI SERVER CONNECT FAILED");
        return "";
    }

    client.print("POST ");
    client.print(PATH);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(HOST);
    client.println("Content-Type: application/json");
    client.print("Authorization: ");
    client.println(API_KEY);
    client.print("Content-Length: ");
    client.println(post_data.length());
    client.println("Connection: close");
    client.println();
    client.print(post_data);

    int status_code = 0;
    bool chunked = false;
    if (!read_http_headers(client, &status_code, &chunked, error_message)) {
        client.stop();
        return "";
    }

    String payload = chunked ? read_chunked_body(client) : read_plain_body(client);
    client.stop();

    if (payload.length() == 0) {
        set_error(error_message, "AI RESPONSE EMPTY");
        return "";
    }

    JsonDocument response_doc;
    DeserializationError error = deserializeJson(response_doc, payload);
    if (error) {
        set_error(error_message, "AI JSON PARSE FAILED");
        return "";
    }

    if (status_code < 200 || status_code >= 300) {
        const char *api_error = response_doc["error"]["message"];
        if (api_error != nullptr && api_error[0] != '\0') {
            if (error_message != nullptr) {
                *error_message = String("AI HTTP ") + status_code + " " + api_error;
            }
        } else if (error_message != nullptr) {
            *error_message = String("AI HTTP ") + status_code;
        }
        return "";
    }

    const char *answer = response_doc["choices"][0]["message"]["content"];
    if (answer == nullptr || answer[0] == '\0') {
        set_error(error_message, "AI ANSWER EMPTY");
        return "";
    }

    return String(answer);
}
