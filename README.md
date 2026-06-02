# SKILL 分支 - 社区实验区

这里接受**任何语言**的原型代码。不会 C++？没关系。用你会的语言写，我们来 review。

## 如何提交

1. 复制 `TEMPLATE/` 文件夹，改名成你的模块名（例如 `my_p2p_module/`）
2. 在 `你的模块名/src/` 里写代码
3. 填写 `你的模块名/README.md`
4. 提 Pull Request 到 `SKILL` 分支

## 目录结构示例

SKILL/
├── TEMPLATE/ # 模板，复制后改名
│ ├── README.md
│ └── src/
│ └── example.py
├── my_p2p_module/ # 你的模块
│ ├── README.md
│ └── src/
│ └── （你的代码）
└── someone_else_module/ # 别人的模块


## 规则

- 必须带 README
- 必须带测试（或可验证的运行命令）
- 代码尽量无 bug
- 审核通过后，你的实现可能被用 C++ 重写进 `main`

## 语言不限

Python、Go、Rust、Java、C#、TypeScript……都可以。

## 审核标准

- 功能正确
- 有测试
- README 写清楚输入输出
- 不恶意

## 注意

SKILL 分支的代码**不会直接合并到 main**。审核通过后，会被重写为 C++ 并入主分支。

你的名字会留在贡献者列表里。
