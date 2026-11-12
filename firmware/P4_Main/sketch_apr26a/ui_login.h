#pragma once

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool ui_login_is_authenticated(void);
void ui_login_show(void);
void ui_login_logout(void);

#ifdef __cplusplus
}
#endif
