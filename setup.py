import os
import sys
import subprocess
from setuptools import setup
from setuptools.command.build_py import build_py

class BuildSharedLib(build_py):
    """
    自定义构建步骤，支持跨平台（Linux, macOS, Windows）编译 C 核心库。
    """
    def run(self):
        print(f">>> 正在为系统 {sys.platform} 编译核心 C 共享库...")
        
        # 确定库文件名和编译命令
        if sys.platform == "win32":
            lib_name = "libkvdb.dll"
            # Windows 下尝试使用 cl (MSVC) 编译，需环境中有 Visual Studio
            # 简化起见，这里假设用户已配置好编译器环境
            # 实际生产中可能需要更复杂的 CMake 或专用构建脚本
            compile_cmd = [
                "cl.exe", "/LD", "/O2", "/Iinclude", "src/kv_store.c",
                "/Fe:libkvdb.dll", "zlib.lib", "User32.lib"
            ]
            # 注意：Windows 下需要预先安装 zlib 并配置好 zlib.lib
        else:
            lib_name = "libkvdb.so"
            # Linux 和 macOS 使用 Makefile
            try:
                subprocess.check_call(['make', 'clean'])
                subprocess.check_call(['make'])
            except subprocess.CalledProcessError as e:
                print(f"编译失败: {e}。请确保已安装 gcc 和 zlib 开发库。")
                raise e

        # 确保库文件已生成
        if not os.path.exists(lib_name):
            # 特殊处理：有些系统 make 可能生成 .dylib (macOS)
            if sys.platform == "darwin" and os.path.exists("libkvdb.dylib"):
                lib_name = "libkvdb.dylib"
            else:
                raise FileNotFoundError(f"核心库 {lib_name} 编译未生成")
            
        # 将生成的共享库拷贝到包目录下，以便打包进 wheel
        # Python 内部会统一加载名为 libkvdb.so 的文件（见 kv_db.py 逻辑）
        # 为了保持 Python 侧逻辑统一，我们在包内统一命名
        target_name = "libkvdb.so" 
        dest = os.path.join(self.build_lib, 'kv_db', target_name)
        self.mkpath(os.path.dirname(dest))
        self.copy_file(lib_name, dest)
        
        # 执行标准构建流程
        super().run()

setup(
    cmdclass={
        'build_py': BuildSharedLib,
    },
)
