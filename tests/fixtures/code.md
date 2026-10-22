# 读代码的时候

代码块是文章里的引文。它需要等宽，需要能横着滑，不需要一整块霓虹。

## 一段 C++

窗口线程只做消息循环。解析放在旁边，做完再把整页换上。

```cpp
void open_file(const std::filesystem::path& path) {
    auto text = read_markdown_file(path);
    if (!text.ok) {
        show_error(text.error);
        return;
    }
    auto page = render_markdown(text.utf8);
    navigate(page);
}
```

行内的 `ICoreWebView2` 不该比周围的句子更大声。注释是灰色，字符串是一种暖色，关键字只是略深，像铅笔在页边做的记号。

## 一段 Python

```python
def slug(title: str) -> str:
    # 给标题一个可以点的名字
    cleaned = title.strip().lower()
    return cleaned.replace(" ", "-") or "section"
```

任务可以记在文稿里，但勾选框不能再改，因为这里不写文件。

- [x] 解析与快照
- [ ] 让窗口真正打开它
- [x] 原始 `<script>alert(1)</script>` 只是文字

表格用来对齐，不用来做卡片。

| 标志 | 作用 |
| --- | --- |
| `MD_DIALECT_GITHUB` | 表格、任务列表、删除线、自动链接 |
| `MD_FLAG_NOHTML` | 原始 HTML 按文字显示 |
