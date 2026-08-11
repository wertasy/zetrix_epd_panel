# Page Registry: Constructor 自动注册（WHOLE_ARCHIVE）

## Goal

用 GCC constructor + ESP-IDF `WHOLE_ARCHIVE` 实现页面自动注册，替代分散在 7 处的手动注册代码，使新增页面从 9 步减至 3 步，消除 ui_manager.c 对 11 个 page 头文件的直接依赖。

## Architecture

每个 page .c 文件通过 `PAGE_REGISTER()` 宏生成一个 GCC constructor（优先级 200），在 `main()` 之前自动注册到全局数组。**ui 组件以 `WHOLE_ARCHIVE` 链接**，强制链接器提取 libui.a 中所有页面对象——否则无外部引用的页面 .obj 永远不会被归档提取，其 constructor 静默不执行（页面"消失"）。

```
page.c:  PAGE_REGISTER(UI_PAGE_FOO, "标题", icon, true, order, &foo_ops, &s_foo.base)
              ↓ 宏展开为 constructor 函数
  _page_register_foo() 在 main() 前执行（libui.a 被 WHOLE_ARCHIVE 全部提取）
              ↓ 调 page_registry_add()
  s_entries[UI_PAGE_FOO] = entry   (O(1) 查找)
              ↓ ui_manager_init()
  page_registry_init() 按 order 排序构建 quick-switch 索引
```

## Key Files

- `components/ui/include/page_registry.h` — page_entry_t 类型 + PAGE_REGISTER 宏 + 查找 API
- `components/ui/page_registry.c` — 注册表实现（s_entries + quick-switch 排序）
- `components/ui/CMakeLists.txt` — `idf_component_register(WHOLE_ARCHIVE ...)`

## 关键决策与教训

1. **WHOLE_ARCHIVE 是 ESP-IDF 原生选项**：`tools/cmake/project.cmake` 检测该属性后在最终链接时用
   `-Wl,--whole-archive <lib> -Wl,--no-whole-archive` 包裹组件库，链接顺序由框架保证。
   手动 `target_link_libraries` 附加的库会追加在链接命令末尾，无法包住 libui.a。

2. **`__attribute__((used))` 无效**：只阻止编译器优化，不阻止链接器 GC 和归档提取。

3. **页面实例必须放 PSRAM**：static 实例占内部 RAM（19 页 ≈ 28 KiB），会压垮 WiFi 驱动
   （`alloc pp wdev funcs fail`）。用 `EXT_RAM_BSS_ATTR`（需
   `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`）。

## 新增页面：3 步

1. `ui_manager.h`：加枚举值
2. 写 `foo_page.h` + `foo_page.c`（底部 `PAGE_REGISTER(...)`）
3. `CMakeLists.txt`：SRCS 加一行

## 消除的代码

| 之前 | 现在 |
|---|---|
| `get_renderer_for_page()` 19-case switch | `page_registry_get_instance(id)` |
| `assign_all_page_ops()` 19 行赋值 | constructor 自动设置 |
| `ui_manager_get_page_title()` switch | `page_registry_get_name(id)` |
| `kQuickSwitchItems[]` 硬编码数组 | `page_registry_quick_switch_items()`（按 order 排序） |
| 19 个 extern ops 声明 | 消除 |
| `struct ui_manager` 嵌入 19 个 page struct | 消除（page.c 自有实例） |
| 11 个 page `#include` | 保留 8 个（数据 API 页面） |
