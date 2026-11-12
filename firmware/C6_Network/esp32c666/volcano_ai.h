#ifndef VOLCANO_AI_H
#define VOLCANO_AI_H

#include <Arduino.h>

bool volcano_ai_is_configured();
String volcano_ai_ask(const String &question, String *error_message);

#endif
