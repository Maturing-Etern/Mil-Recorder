
set -o pipefail
GCC='E:/Qt/Tools/mingw1310_64/bin/g++.exe'
P='E:/grain_data/packages'
M='E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder/milize_js'
SRC='E:/grain_data/build/source.cpp.patched.cpp'
OUT="${1:-E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder/mil-recorder_v4.exe}"
R='E:/HugoMoveData/User/lenovo/Downloads/Milthm unpack tools/Mil-Recorder'
WD='E:/grain_data/build/tmp'
mkdir -p "$WD"
cd "$R"
'E:/Qt/Tools/mingw1310_64/bin/windres.exe' mil_recorder.rc -O coff -o "$WD/mil_recorder_res.o" || exit 1
ARGS=(-std=c++20 -static -O1 -Wsign-compare -Wa,-mbig-obj -include "$M/milize_js.h")
for pkg in gglfw3==0 gimage==0 gvorbis==0 gminiaudio==0 gvideoenc==0 gtext_renderer==0; do
  ARGS+=("-I$P/$pkg/.grain/includes")
done
ARGS+=("-I$M")
ARGS+=("$SRC" "$M/libmilize_js.a" "$WD/mil_recorder_res.o")
for lib in \
  "$P/gglfw3==0/.grain/libs/libglfw3_windows_x86_64.a" \
  "$P/gimage==0/.grain/libs/libstb_image_windows_x86_64.a" \
  "$P/gvorbis==0/.grain/libs/libstb_vorbis_windows_x86_64.a" \
  "$P/gminiaudio==0/.grain/libs/libminiaudio_windows_x86_64.a" \
  "$P/gvideoenc==0/.grain/libs/libavformat.a" \
  "$P/gvideoenc==0/.grain/libs/libavcodec.a" \
  "$P/gvideoenc==0/.grain/libs/libavutil.a" \
  "$P/gvideoenc==0/.grain/libs/libx264.a" \
  "$P/gvideoenc==0/.grain/libs/libfdk-aac.a" \
  "$P/gtext_renderer==0/.grain/libs/libstb_truetype_windows_x86_64.a"; do
  ARGS+=("$lib")
done
ARGS+=(-lbcrypt -luuid -lole32 -lcomctl32 -lopengl32 -lgdi32 -lws2_32 -o "$OUT")
echo "G++ build start: $(date +%T)"
"$GCC" "${ARGS[@]}"
rc=$?
echo "GPP_RC=$rc"
exit $rc
