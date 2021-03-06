REPO_PATH ?= ../..
SYS_CONF_DEFAULTS ?= $(REPO_PATH)/fw/src/fs/conf_sys_defaults.json
APP_CONF_DEFAULTS ?= $(REPO_PATH)/fw/src/fs/conf_app_defaults.json
PYTHON ?= python

$(SYS_CONFIG_C): $(SYS_CONF_DEFAULTS) $(APP_CONF_DEFAULTS) $(REPO_PATH)/tools/json_to_c_config.py
	$(vecho) "GEN   $@"
	$(Q) $(PYTHON) $(REPO_PATH)/tools/json_to_c_config.py \
	  --c_name=sys_config \
	  --dest_dir=$(dir $@) $(SYS_CONF_DEFAULTS) $(APP_CONF_DEFAULTS)
