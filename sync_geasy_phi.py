# 把 Mil-Recorder (grain library 项目) 同步到 grain 的本地包缓存。
#
# 背景(实测):
#   files/test/info.json 的 externals 含 "geasy_phi==0", 指向 <data_dir>/packages/geasy_phi==0/。
#   该包目录没有 .grain/version -> grain.local.get_version() 返回 None -> is_local=True
#   -> grain 直接读 <pkg>/info.json 的 requirements.embeds 与 <pkg>/lib.hpp (见 grain/build.py process())。
#   注意: 此时 .grain/embeds/catalog.json **不被使用**(那是 not is_local 分支)。
#
#   所以 Mil-Recorder/ 才是真源; 改完 lib.hpp / files/resources / info.json 后跑本脚本同步,
#   否则 regen 出来的 source.cpp 会静默用旧代码(历史上 Fracture 补丁"没生效"就是这么来的)。
#
# 用法: python sync_geasy_phi.py [--dry]
import shutil, pathlib, sys, datetime

ROOT = pathlib.Path(__file__).resolve().parent
PKG = pathlib.Path(r'E:/grain_data/packages/geasy_phi==0')

FILES = ['lib.hpp', 'info.json', 'files/test/main.cpp']   # 整文件覆盖
SRC_DIR = ROOT / 'files' / 'resources'
DST_DIR = PKG / 'files' / 'resources'            # 只增/覆盖, 不删除包内多出的文件

def main():
    dry = '--dry' in sys.argv
    if not PKG.exists():
        print('package dir not found: %s' % PKG)
        return 1
    ts = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    changed = 0

    for name in FILES:
        src, dst = ROOT / name, PKG / name
        if not src.exists():
            print('[MISS] %s' % src)
            continue
        same = dst.exists() and dst.read_bytes() == src.read_bytes()
        print('[%s] %-12s %d bytes' % ('skip' if same else 'sync', name, src.stat().st_size))
        if not same and not dry:
            if dst.exists():
                shutil.copy2(dst, dst.with_suffix(dst.suffix + '.bak_' + ts))
            shutil.copy2(src, dst)
            changed += 1

    for src in sorted(SRC_DIR.rglob('*')):
        if not src.is_file():
            continue
        rel = src.relative_to(SRC_DIR)
        dst = DST_DIR / rel
        if dst.exists() and dst.read_bytes() == src.read_bytes():
            continue
        print('[sync] resources/%s' % rel.as_posix())
        if not dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        changed += 1

    print('done: %d changed%s' % (changed, ' (dry)' if dry else ''))
    return 0

if __name__ == '__main__':
    sys.exit(main())
