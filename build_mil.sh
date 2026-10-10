#!/usr/bin/env bash
# Mil-Recorder 一键重编译（2026-09-04 定稿, g++ include 必须正斜杠+引号否则被吞）
# 用法: bash build_mil.sh   (需先确保 source.cpp.patched.cpp 已含全部补丁)
set -e
export PATH="E:/Qt/Tools/mingw1310_64/bin:$PATH"
ROOT="E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder"
SRC="E:/grain_data/build/source.cpp.patched.cpp"
G="E:/grain_data/packages"

# 编译临时文件重定向到 E 盘（避免占 C 盘 Temp）+ -pipe 减少中间文件
export TMP="E:/grain_data/build/tmp" TEMP="E:/grain_data/build/tmp"
mkdir -p "E:/grain_data/build/tmp"

# 应用图标: windres 把 .rc 编成 .o 再参与链接
# 注意: windres 会把 -I 与输入路径二次传参给 cc1, 含空格的绝对路径必被撕裂
#       => 必须先 cd 到 ROOT, 只传相对文件名, 绝不传任何含空格的路径
cd "$ROOT"
"E:/Qt/Tools/mingw1310_64/bin/windres.exe" mil_recorder.rc -O coff -o "$TMP/mil_recorder_res.o"
echo "windres ok"

"E:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -std=c++20 -static -O1 -Wsign-compare -Wa,-mbig-obj -pipe \
  -include "$ROOT/milize_js/milize_js.h" \
  -I"$ROOT" \
  -I"$G/gglfw3==0/.grain/includes" \
  -I"$G/gimage==0/.grain/includes" \
  -I"$G/gvorbis==0/.grain/includes" \
  -I"$G/gminiaudio==0/.grain/includes" \
  -I"$G/gvideoenc==0/.grain/includes" \
  -I"$G/gtext_renderer==0/.grain/includes" \
  -I"$ROOT/milize_js" \
  -I"$ROOT/external/miniz" \
  "$SRC" \
  "$ROOT/external/miniz/miniz.c" \
  "$ROOT/external/miniz/miniz_tinfl.c" \
  "$ROOT/external/miniz/miniz_tdef.c" \
  "$ROOT/external/miniz/miniz_zip.c" \
  "$ROOT/milize_js/libmilize_js.a" \
  "$TMP/mil_recorder_res.o" \
  "$G/gglfw3==0/.grain/libs/libglfw3_windows_x86_64.a" \
  "$G/gimage==0/.grain/libs/libstb_image_windows_x86_64.a" \
  "$G/gvorbis==0/.grain/libs/libstb_vorbis_windows_x86_64.a" \
  "$G/gminiaudio==0/.grain/libs/libminiaudio_windows_x86_64.a" \
  "$G/gvideoenc==0/.grain/libs/libavformat.a" \
  "$G/gvideoenc==0/.grain/libs/libavcodec.a" \
  "$G/gvideoenc==0/.grain/libs/libavutil.a" \
  "$G/gvideoenc==0/.grain/libs/libx264.a" \
  "$G/gvideoenc==0/.grain/libs/libfdk-aac.a" \
  "$G/gtext_renderer==0/.grain/libs/libstb_truetype_windows_x86_64.a" \
  -lbcrypt -luuid -lole32 -lcomctl32 -lopengl32 -lgdi32 -lws2_32 \
  -o "$ROOT/mil-recorder.exe"
echo "build ok"
