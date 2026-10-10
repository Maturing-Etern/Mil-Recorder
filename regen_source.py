from grain import config, build
from pathlib import Path

cfg = config.Config.from_default()
repo = cfg.get_storage()
data_dir = cfg.data_dir_as_path()
app_dir = Path(r"E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder/files/test")
local_repo = Path(r"E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder")

bc = build.BuildConfig(
    output=str(data_dir / "build" / "main"),
    macros=["APP_TYPE_MIL_PLAYER"],
    externals=[local_repo],
)
fs = build.generate_final_source(repo, data_dir, app_dir, bc)
out = data_dir / "build" / "source.cpp"
out.write_text(fs.code, encoding="utf-8")
print("code len:", len(fs.code))
print("wrote:", out)
