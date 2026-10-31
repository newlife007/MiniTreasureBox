# MiniTreasureBox

小小百宝箱是一款面向桌面的 AI 小工具、实用工具和轻量小游戏集合应用。

## 构建要求

- CMake 3.22 或更高版本
- 支持 C++20 的编译器
- Qt 6 的 Core、Gui、Widgets 和 Test 组件
- Python 3.10 或更高版本，用于发布打包脚本和对应的回归测试

## 配置、构建与测试

Qt 安装在标准系统路径时：

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

本开发环境中的 Qt 6.8.3 需要显式提供安装前缀：

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH='/projects/sandbox/Qt/6.8.3/gcc_64;/projects/sandbox/.qt-sysroot/usr'
cmake --build build --parallel
LD_LIBRARY_PATH=/projects/sandbox/Qt/6.8.3/gcc_64/lib:/projects/sandbox/.qt-sysroot/usr/lib64 \
  ctest --test-dir build --output-on-failure
```

CMake 会为需要创建 Qt 窗口的测试自动设置 `QT_QPA_PLATFORM=offscreen`，因此上述 CTest 命令可直接在无显示服务器的环境中运行。

## 运行

标准 Qt 安装环境：

```bash
./build/MiniTreasureBox
```

本开发环境：

```bash
LD_LIBRARY_PATH=/projects/sandbox/Qt/6.8.3/gcc_64/lib:/projects/sandbox/.qt-sysroot/usr/lib64 \
  ./build/MiniTreasureBox
```

## 项目文档

- [产品文档](docs/product/product-requirements.md)
- [技术文档](docs/technical/technical-design.md)

技术方案采用 Qt 6、C++ 和 CMake，AI 推理基于 ONNX Runtime C++。

## 许可协议 / License

本项目采用 [PolyForm Noncommercial License 1.0.0](LICENSE) 协议，**仅限非商业用途**。

✅ **允许**：

-   个人学习、研究、实验、测试
    
-   个人兴趣项目、业余爱好
    
-   慈善组织、教育机构、公共科研机构、政府机构使用
    
-   在非商业目的下修改、二次开发和分发（需保留本协议及版权声明）
    

❌ **禁止**：

-   任何商业用途，包括但不限于：用于商业产品、对外提供收费服务、企业内部营利性使用
    

📩 **商业授权**：如需商业使用，请联系 [544121799@qq.com](mailto:544121799@qq.com) 获取单独授权。

> This project is licensed under the PolyForm Noncommercial License 1.0.0. Commercial use requires a separate license — contact [544121799@qq.com](mailto:544121799@qq.com).

Copyright © 2026 [544121799@qq.com](mailto:544121799@qq.com)