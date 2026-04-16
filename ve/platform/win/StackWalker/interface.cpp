//
// Created by lqi on 2025/7/14.
//

#include "interface.h"

#include "BaseException.h"

void set_default_handler() { SetUnhandledExceptionFilter(CBaseException::UnhandledExceptionFilter); }

void set_default_exception() { _set_se_translator(CBaseException::STF); }