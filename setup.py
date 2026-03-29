import os
import platform
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import shutil


class BuildExt(build_ext):
    """
    自定义 build_ext 步骤，确保编译后的库文件名统一，
    以便 ctypes 能够跨平台（Linux/macOS/Windows）一致地加载。
    """
    def run(self):
        super().run()
        for ext in self.extensions:
            if ext.name == "kv_db.libkvdb":
                ext_path = self.get_ext_fullpath(ext.name)
                dest_dir = os.path.dirname(ext_path)
                
                # 根据平台确定目标库文件名
                if platform.system() == "Windows":
                    target_name = "libkvdb.dll"
                elif platform.system() == "Darwin":
                    target_name = "libkvdb.dylib"
                else:
                    target_name = "libkvdb.so"
                
                target_path = os.path.join(dest_dir, target_name)
                if os.path.exists(ext_path):
                    shutil.copyfile(ext_path, target_path)
                    print(f">>> 已将编译产物 {ext_path} 拷贝并重命名为 {target_path}")


# 根据平台配置库依赖
libraries = ["z"]
if platform.system() != "Windows":
    libraries.append("pthread")

# 定义 C 扩展
kv_extension = Extension(
    "kv_db.libkvdb",
    sources=["src/kv_store.c"],
    include_dirs=["include"],
    libraries=libraries,
    extra_compile_args=["-std=c11"],
)

setup(
    ext_modules=[kv_extension],
    cmdclass={
        'build_ext': BuildExt,
    },
)
