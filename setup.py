import os
import sys
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import shutil

class BuildExt(build_ext):
    """
    自定义 build_ext 步骤，确保编译后的库文件名统一为 libkvdb.so，
    以便 ctypes 能够跨平台一致地加载。
    """
    def run(self):
        super().run()
        # 获取编译后的库文件路径
        for ext in self.extensions:
            if ext.name == "kv_db.libkvdb":
                ext_path = self.get_ext_fullpath(ext.name)
                dest_dir = os.path.dirname(ext_path)
                # 统一命名为 libkvdb.so (Windows 下也是如此)
                # 这样 kv_db.py 就不需要根据平台动态修改加载逻辑
                target_path = os.path.join(dest_dir, "libkvdb.so")
                if os.path.exists(ext_path):
                    shutil.copyfile(ext_path, target_path)
                    print(f">>> 已将编译产物 {ext_path} 拷贝并重命名为 {target_path}")

# 定义 C 扩展
# 使用 setuptools 自动处理跨平台编译器调用、头文件包含和库链接
kv_extension = Extension(
    "kv_db.libkvdb",
    sources=["src/kv_store.c"],
    include_dirs=["include"],
    libraries=["z", "pthread"] if sys.platform != "win32" else ["zlib", "User32"],
    # Windows 下 setuptools 会自动处理 MSVC 环境
)

setup(
    ext_modules=[kv_extension],
    cmdclass={
        'build_ext': BuildExt,
    },
)
