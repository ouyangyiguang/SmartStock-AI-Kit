#ifndef _AFE_MODEL_PATH_H_
#define _AFE_MODEL_PATH_H_

#include "esp_log.h"
#include "model_path.h"

static const char *WN_MODEL_NAME = "wn9_customword";

static srmodel_data_t wakenet_data = {
    .num = 1,
    .files = (char*[]){ "wn9_customword/wn9_data" },
    .data = NULL,
    .sizes = NULL
};

static srmodel_list_t wakenet_models = {
    .model_name = (char*[]){ "wn9_customword" },
    .model_info = (char*[]){ "WakeNet9_v1h24" },
    .mmap_handle = NULL,
    .num = 1,
    .model_data = (srmodel_data_t*[]){ &wakenet_data }
};

#endif