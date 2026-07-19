# SKILL 分支 - 社区实验区

这里接受**任何语言**的原型代码。不会 C++？没关系。

## 目录结构

你的代码放在这里：

```text
SKILL/
├── TEMPLATE/                    # 模板（不要改）
├── 你的模块名/                   # 用你的模块名
│   ├── README.md                # 必须写
│   └── src/                     # 你的代码
│       └── （任意语言）
└── 别人的模块/
```

## 如何提交

1. 复制 `TEMPLATE/` 文件夹，改名成你的模块名（用 `_` 分隔单词）
2. 在 `你的模块名/src/` 里写代码
3. 填写 `你的模块名/README.md`
4. 提 Pull Request 到 `SKILL` 分支

## PR 标题格式

`[SKILL] 模块名 - 简短描述`

示例：

- `[SKILL] p2p_dht - Python Kademlia implementation`
- `[SKILL] gui_demo - Avalonia message input UI`

## 规则

- 必须有自己的文件夹
- 必须带 `README.md`
- 代码必须能跑（不保证无 bug，但不能故意破坏）
- 语言不限

## 审核后

你的代码会被 review。功能正确的话，我们会用 C++ 重写进 `main` 分支。

你的名字会留在贡献者列表里。

## 注意

`SKILL` 分支的代码**不会直接合并到 main**。只做原型验证。
