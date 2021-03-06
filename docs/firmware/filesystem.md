---
title: Filesystem
---

Mongoose IoT uses SPIFFS filesystem on some of the boards (e.g. ESP8266, CC3200).
SPIFFS is a flat filesystem, i.e. it has no directories. To provide the same
look at feel on all platforms, Mongoose IoT uses flat filesystem on all
architectures.

Below there is a description of the files and their meaning.  System
files, not supposed to be edited:

- `sys_init.js`: This file is called by the C code
  on firmware startup.
  In turn, this file calls `app.js` file which
  contains user-defined code.
- `conf_sys_defaults.json`: System configuration parameters. Can be
  overridden in `conf.json`.
- `conf_sys_schema.json`: Contains description of the system configuration,
  used by the Web UI to render the configuration page.
- `conf.json`: This file can be absent. It is created
  when user calls `Sys.conf.save()` function, or by the Web UI when user
  saves configuration. `conf.json` contains only overrides to system
  and app config files.
- `index.html`: Configuration Web UI file.
- `sys_*.js`: Various drivers.

Files that are meant to be edited by developers:

- `app.js`: Application-specific initialization file.
  This file is called by `sys_init.js`. User code must go here.
- `conf_app_defaults.json`: Application-specific configuration file. Initially
  empty.  If application wants to show it's own config parameters on the
  configuration Web UI, those parameters should go in this file.
- `conf_app_schema.json`: Description of the app-specific config options.
