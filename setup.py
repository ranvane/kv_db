import os
import sys
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import shutil

class BuildExt(build_ext):
    """
    自定义 build_ext 步骤，确保编译后的库文件名统一为 libkvdb.so，
    以便 ctypes 能够跨平台（Linux/macOS）一致地加载。
    """
    def run(self):
        super().run()
        for ext in self.extensions:
            if ext.name == "kv_db.libkvdb":
                ext_path = self.get_ext_fullpath(ext.name)
                dest_dir = os.path.dirname(ext_path)
                target_path = os.path.join(dest_dir, "libkvdb.so")
                if os.path.exists(ext_path):
                    shutil.copyfile(ext_path, target_path)
                    print(f">>> 已将编译产物 {ext_path} 拷贝并重命名为 {target_path}")

# 定义 C 扩展
# 仅支持 POSIX 环境（Linux, macOS）
kv_extension = Extension(
    "kv_db.libkvdb",
    sources=["src/kv_store.c"],
    include_dirs=["include"],
    libraries=["z", "pthread"],
    extra_compile_args=["-std=c11", "-D_POSIX_C_SOURCE=200809L"],
)

setup(
    ext_modules=[kv_extension],
    cmdclass={
        'build_ext': BuildExt,
    },
)
