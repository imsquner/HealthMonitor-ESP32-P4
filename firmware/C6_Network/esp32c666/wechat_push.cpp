#include "wechat_push.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

static const uint32_t WECHAT_TIMEOUT_MS = 15000;

static void set_error(String *error_message, const String &text)
{
    if (error_message != nullptr) {
        *error_message = text;
    }
}

static bool webhook_url_valid(const char *url, String *error_message)
{
    if (url == nullptr || url[0] == '\0') {
        set_error(error_message, "URL empty");
        return false;
    }

    String url_str(url);
    url_str.trim();
    if (!url_str.startsWith("https://")) {
        set_error(error_message, "URL scheme must be https");
        return false;
    }
    if (url_str.indexOf("YOUR_") >= 0) {
        set_error(error_message, "webhook key placeholder");
        return false;
    }
    if (url_str.indexOf("/cgi-bin/webhook/send?key=") < 0) {
        set_error(error_message, "URL is not a WeCom webhook");
        return false;
    }
    return true;
}

static bool wechat_push_send(const char *webhook_url,
                             const char *msgtype,
                             const char *content_key,
                             const String &content,
                             String *error_message)
{
    if (WiFi.status() != WL_CONNECTED) {
        set_error(error_message, "WiFi not connected");
        return false;
    }
    if (!webhook_url_valid(webhook_url, error_message)) {
        return false;
    }

    JsonDocument request_doc;
    request_doc["msgtype"] = msgtype;
    request_doc[content_key]["content"] = content;

    // Use a temporary heap buffer to avoid the Arduino ESP32 Core 3.1.2 String = nullptr crash in ArduinoJson 7
    size_t json_len = measureJson(request_doc);
    char* post_buf = (char*)malloc(json_len + 1);
    if (post_buf == nullptr) {
        set_error(error_message, "Out of memory");
        return false;
    }
    serializeJson(request_doc, post_buf, json_len + 1);
    String post_data(post_buf);
    free(post_buf);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(WECHAT_TIMEOUT_MS);

    if (!http.begin(client, webhook_url)) {
        set_error(error_message, "HTTP begin failed");
        return false;
    }

    http.addHeader("Content-Type", "application/json; charset=utf-8");
    const int http_code = http.POST(post_data);
    const String payload = http.getString();
    http.end();

    if (http_code <= 0) {
        set_error(error_message, String("HTTP POST failed ") + http.errorToString(http_code));
        return false;
    }
    if (payload.length() == 0) {
        set_error(error_message, String("HTTP ") + http_code + " empty body");
        return false;
    }

    JsonDocument response_doc;
    DeserializationError json_error = deserializeJson(response_doc, payload);
    if (json_error) {
        set_error(error_message, String("HTTP ") + http_code + " JSON parse failed: " + payload.substring(0, 120));
        return false;
    }

    const int errcode = response_doc["errcode"] | -1;
    const char *errmsg_raw = response_doc["errmsg"];
    const char *errmsg = (errmsg_raw != nullptr) ? errmsg_raw : "";

    if (http_code < 200 || http_code >= 300) {
        set_error(error_message, String("HTTP ") + http_code + " errcode " + errcode + " " + errmsg);
        return false;
    }
    if (errcode != 0) {
        set_error(error_message, String("HTTP ") + http_code + " errcode " + errcode + " " + errmsg);
        return false;
    }

    set_error(error_message, String("HTTP ") + http_code + " errcode 0 ok");
    return true;
}

bool wechat_push_send_markdown(const char *webhook_url,
                               const String &markdown_content,
                               String *error_message)
{
    return wechat_push_send(webhook_url, "markdown", "markdown",
                            markdown_content, error_message);
}

bool wechat_push_send_text(const char *webhook_url,
                           const String &text_content,
                           String *error_message)
{
    return wechat_push_send(webhook_url, "text", "text",
                            text_content, error_message);
}
