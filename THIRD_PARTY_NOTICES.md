# 第三方组件与数据来源说明

本项目包含或依赖以下第三方成果，特此致谢并声明授权关系。

---

## 1. FFReader / 幻舞奇影 FinanceFileReader

- 项目主页：<https://www.ffreader.cn/>
- 代码仓库：<https://gitee.com/cnldw/FinanceFileReader>
- 作者：幻舞奇影（刘德位，793554262@qq.com）
- 许可证：GNU Lesser General Public License v3.0（LGPL-3.0）

### 使用范围

本项目 `conf/` 目录下的以下文件复制自 FFReader 发布包（可能做过文件名或内容上的少量整理）：

```
conf/OFD_20.ini
conf/OFD_21.ini
conf/OFD_22.ini
conf/OFD_Dictionary.ini
conf/OFD_CodeInfo.ini
conf/FIXED_WDEP_理财产品中央数据交换协议_V1.0.ini
conf/FIXED_WDEP_理财产品中央数据交换协议_V1.1.ini
conf/FIXED_WPID_理财登信息披露.ini
conf/A_配置说明_配置前必读.txt
```

这些文件属于接口字段定义与业务字典数据，按 LGPL-3.0 授权使用。它们的原始版本可从上述仓库获取。

### 说明

- 本项目的 **C++ 程序代码为独立实现**，未复制 FFReader 的源代码，也未链接 FFReader 的二进制库。
- `conf/` 中的 INI 文件是运行期读取的纯数据配置，程序本身不依赖 FFReader 的任何可执行文件或库。
- 若你需要分发本项目，请保留本说明，并遵守 LGPL-3.0 对配置数据的要求（保留版权声明、提供对应源码/原始文件获取途径）。

---

## 2. Qt 5.15.2

- 项目主页：<https://www.qt.io/>
- 许可证：LGPL-3.0 / GPL-3.0 / 商业许可（按组件）

本项目以动态链接方式使用 Qt（`Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll` 等），符合 LGPL-3.0 对动态链接的要求。用户可以用自行编译的 Qt 5.15.2 动态库替换程序目录中的同名 DLL。

Qt 5.15.2 是 Qt 官方最后一个支持 Windows 7 的版本。

---

## 3. 示例数据

`conf/demo/` 下的示例文件由 `tools/make_demo.py` 依据接口配置自动生成，不包含任何真实客户或机构数据，可自由使用。
