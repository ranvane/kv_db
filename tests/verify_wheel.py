import os
import shutil
from kv_db import KVDB, KVType

def verify_installation():
    """验证 wheel 安装后的包是否工作正常"""
    db_path = "./verify_db"
    if os.path.exists(db_path):
        shutil.rmtree(db_path)
    
    print(">>> 正在验证 KVDB 安装...")
    try:
        db = KVDB(db_path)
        
        # 测试基本写入和读取
        db.set("install_test", "Success")
        val = db.get("install_test")
        assert val == "Success", f"期望 Success, 实际 {val}"
        
        # 测试复杂类型
        db.set("json_data", {"status": "ok", "version": 1.0})
        json_val = db.get("json_data")
        assert json_val["status"] == "ok"
        
        print("✓ 核心功能验证：通过")
        
        # 测试事务
        db.begin()
        db.set("txn_test", True)
        assert db.get("txn_test") is None
        db.commit()
        assert db.get("txn_test") is True
        
        print("✓ 事务功能验证：通过")
        
        db.close()
        print("✅ KVDB Wheel 安装包验证完成！")
        
    except Exception as e:
        print(f"❌ 验证失败: {e}")
        exit(1)
    finally:
        if os.path.exists(db_path):
            shutil.rmtree(db_path)

if __name__ == "__main__":
    verify_installation()
