#ifndef WECHAT_PUSH_H
#define WECHAT_PUSH_H

#include <Arduino.h>

bool wechat_push_send_markdown(const char *webhook_url,
                               const String &markdown_content,
                               String *error_message = nullptr);

bool wechat_push_send_text(const char *webhook_url,
                           const String &text_content,
                           String *error_message = nullptr);

#endif
