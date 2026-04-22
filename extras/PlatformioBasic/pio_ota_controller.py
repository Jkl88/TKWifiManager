# pre: — подставить -D TKWM_OTA_CONTROLLER=... из custom_upload_controller (см. platformio.ini)
Import("env")
import re

def _opt():
    gpo = getattr(env, "GetProjectOption", None)
    if gpo is None:
        return ""
    try:
        v = gpo("custom_upload_controller", "")
    except TypeError:
        v = gpo("custom_upload_controller")
    if v is None:
        return ""
    return str(v).strip()

val = _opt()
if not val:
    pass
elif re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", val):
    env.Append(CXXFLAGS=[f"-DTKWM_OTA_CONTROLLER={val}"])
else:
    print("pio_ota_controller: custom_upload_controller must be one C token (e.g. ESP32, My_ctrl); ignored:", repr(val))
