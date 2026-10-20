# Godot AI 助手

[English](README.md) | **简体中文**

内置于 Godot 编辑器的 AI 助手，以原生模块 `ai_assistant` 的形式编译进引擎，适配 **Godot 4.7+**（godot-master）。

它在编辑器里增加一个停靠的 AI 面板，支持多家大模型服务商，能直接在编辑器里完成场景编辑、脚本编写、项目设置、带文件上下文的提问、素材生成和调试辅助。

功能概览另见 [FEATURES.zh-CN.md](FEATURES.zh-CN.md)。

## 概述

`ai_assistant` 是原生 C++ 引擎模块，不是 GDExtension，也不是以项目文件形式分发的普通插件。它放在 Godot 引擎源码树里，随编辑器一起编译。

启用后，模块会注册：

- 编辑器插件 `AIAssistantPlugin`
- 主停靠面板 `AIAssistantPanel`
- Anthropic、OpenAI 兼容接口、Gemini、DeepSeek、GLM 的服务商接入
- 回复解析、脚本执行、上下文收集、权限、检查点、错误监视、网页搜索、素材生成、`@` 提及输入等辅助系统
- 按需注入的 API 速查和避坑经验（与话题无关时不占提示词）

面板挂在编辑器右侧的停靠区。

## 已实现的功能

### 编辑器助手面板

- 停靠在 Godot 编辑器里的 AI 面板
- 聊天式界面，对话历史持久保存
- `New`、`History`、`Settings`、附加文件、提及、发送、停止等控件
- 流式显示回复，可用 `Stop` 中途停止
- 回复按 Markdown 渲染，流式输出时同样生效：标题、粗体 / 斜体 / 删除线、行内代码、围栏代码块、嵌套列表与有序列表、任务列表、引用、分隔线、表格，以及可在浏览器中打开的网页链接
- 多行输入框自动调整高度
- 高频预设操作：
  - `Fix Errors`（修复报错）
  - `Performance`（性能分析）

### 没有模式：由消息决定

不再有 ASK / AGENT / PLAN 的切换。用户的消息要什么，助手就做什么：

- 提问就回答。模型可以用工具查看项目，但不做任何修改。
- 要求做事就去做。
- 要求写计划（“计划一下……”“你会怎么做……”“先列步骤”）就给出计划：模型调用 `propose_plan` 提交步骤，这一轮不做任何修改（没有工具的模型以 `[PLAN]` 开头回复）。计划连同清单文件显示出来，用户什么时候想开始，就逐步执行，每步一次请求。

不确定用户是否要改动时，模型会先说明做法，并提出可以代为完成。每一次修改仍受权限设置约束，所以一个提问不会变成用户没有允许的修改。

### 节点与文件提及

- 在输入框里输入 `@` 打开场景节点的补全；`@` 后面至少有一个字符时，还会列出匹配的项目文件
- 从场景面板拖入节点，或从文件系统面板拖入文件、文件夹，会在输入框里插入提及标签
- 工具栏的 `@` 按钮可提及当前选中的节点
- 在场景面板的节点或文件系统面板的条目上右键，可从菜单里提及
- 提及标签在输入框内联显示（文件为绿色，节点为蓝色），发送前展开：节点展开为结构化的节点上下文，文件展开为内容（上限 50,000 字符），文件夹展开为条目列表
- 提及占位符是单字符的原子标记，退格一次即可整块删除

### 文件附件

- 通过编辑器的文件选择器为下一次请求附加项目文件
- 附件内容作为明确的上下文追加到发出的提示词里
- 支持的附件类型：
  - Markdown / 图表：`*.md`、`*.mmd`、`*.mermaid`
  - 文本 / 数据：`*.txt`、`*.json`、`*.yaml`、`*.yml`、`*.toml`、`*.csv`
  - GDScript：`*.gd`、`*.gdscript`

### 服务商支持

- Anthropic（Claude）
- OpenAI
- Gemini
- DeepSeek
- GLM（智谱 AI）

OpenAI 这一项也可以通过端点设置指向兼容的自定义接口。

模型是一个文本框，任意模型名都能填，这正是 OpenAI 兼容的中转或聚合服务（硅基流动、OpenRouter、本地服务）所需要的；把它的地址填到 API 端点即可。文本框旁的按钮列出已知模型，`测试 / 刷新` 会把列表换成端点返回的那一份：服务商官方接口只保留当前的对话模型，其他服务则完整列出。每家服务商各存一份 API 密钥，切换服务商时自动换用。

OpenAI、DeepSeek、GLM 使用同一种 chat-completions 协议，共用一份实现（`OpenAICompatibleProvider`），各自只声明不同之处（端点、模型表、输出上限字段的名字）。

### 模型参数与思考强度

模型能用多少、支持什么，取自 [models.dev](https://models.dev)，它按服务商和模型记录：上下文窗口、输出上限、是否接受温度、有哪些思考选项。插件内置一份（`data/model_specs.json`，用 `python update_model_specs.py` 更新）；编辑器发现自己那份超过一周时，也会自动下载新数据，存在 `<编辑器配置目录>/ai_assistant/model_specs.json`。数据里没有的模型，退回到服务商自带的表。

- **最大 Token 数**默认跟随所选模型：取它的输出上限，但不超过上下文窗口的四分之一，给提示词和历史留够空间。勾选“自定义”可以固定一个值。
- **温度**默认用模型自己的默认值，勾选“自定义”才发送；模型不接受温度时这一项不可用。
- **思考强度**列出所选模型自己提供的选项，用厂商的原词：DeepSeek V4 是 `low` / `high` / `max`，当前的 Claude 是 `low` 到 `max` 五档，GPT 是 `none` 到 `xhigh`，部分 Gemini 是 `minimal` 到 `high`，能关闭思考的模型还有“关闭”。选“模型默认”则不发送这个参数。各家用各自的参数名发送（`reasoning_effort`、`thinking.type`、`output_config.effort`、`thinkingConfig.thinkingLevel`）。

只能设思考预算的老模型（Claude Haiku 4.5、Gemini 2.5）给出 4096 / 16384 / 32768 tokens 几档预算，不超过模型允许的范围。

默认模型也来自这份数据：取各家旗舰系列里最新的那个（Claude 的 `claude-opus`、OpenAI 的 `gpt-sol`、Gemini 的 `gemini` / `gemini-pro`、GLM 的 `glm`、DeepSeek 的 `deepseek-thinking`），数据更新后默认模型跟着变。从服务商拉到模型列表时，列表里有默认模型就用它；没有的话，先按名字里的版本号挑（比数据更新的一代会先出现在这里），再按数据里的发布日期挑。

思考型模型在工具调用循环里需要拿回自己之前的推理：

- DeepSeek 和 GLM 要求把之前回复的推理内容（`reasoning_content`）一并传回，插件会替它们传。
- Claude 的思考块带签名，插件把当前这一轮工具循环里的思考块原样传回，并请 API 把失效的块丢掉而不是拒绝请求（`block_binding.prefix_mismatch_behavior: "drop_block"`）。系统提示词里带着编辑器的当前状态，每次请求都会变，所以有些块会失效；即便如此，也不会比不传更差。中转服务若不接受这个参数，插件会自动改为不传、重试一次。

思考型模型调用工具时，正文常常是空的，只有推理和工具调用；这种回复按工具调用执行，不会再被当成“只有推理、没有回答”。

### 按需注入 API 速查（`AIAPIDocLoader`）

系统提示词保持精简。`AIAPIDocLoader` 不会静态塞入全部 Godot API 文档，而是识别用户消息里提到的类名（按词边界匹配），只为这些类注入简明的 API 速查。覆盖 50 多个常用类，包括 `CharacterBody3D`、`NavigationAgent3D`、`AnimationPlayer`、`RigidBody3D` 等。

### 按需注入避坑经验（`AIGotchasIndex`）

`knowledge/gotchas.md` 里存有 395 条 Godot 特有的坑，每条带关键词。请求时只注入关键词与用户消息匹配的条目，最相关的排在前面，并有 3000 字符的硬上限。它取代了过去不管相关与否、每次请求都注入的静态段落。

匹配时忽略通用关键词（普通英文单词、`gdscript`、`node`、`int` 等），其余按稀有程度加权，因此一条经验至少要命中一个足够具体的词（通常是类名或方法名）才会被注入。没有提到具体 API 的消息不会得到任何避坑经验。类名紧贴中日韩文字书写时也能匹配。

常驻的基础提示词，对用工具工作的模型约 1.1 万字符（约 3 千 token），对写代码块的模型约 3.3 万字符，后者还得学会 `EditorScript` 怎样搭建场景。两者都在 `system_prompt.md` 里：不是所有模型都需要的内容夹在 `<!-- SCRIPT-ONLY -->`、`<!-- TOOLS-ONLY -->`、`<!-- MARKERS-ONLY -->` 标记行之间。

### 领域提示（`AIDomainPrompts`）

针对特定领域的请求（界面 / Control 布局、物理、导航、着色器等），按需在基础提示词之外注入专门的提示片段。

### 代码生成与执行

- 解析 AI 回复，提取可执行的 GDScript 代码块
- 把生成的代码包装成 `EditorScript`
- 在编辑器上下文中执行
- 执行前可只做编译检查
- 执行前按安全策略检查生成的代码
- 敏感操作接入权限检查
- 流式回复被手动停止时，已收到的部分代码保留在历史里，但不执行

编辑器内执行代码的安全策略（在词法单元上检查，因此注释和字符串内容不会被当成代码）：

- `OS`、`Engine`、`ClassDB` 只能以 `名字.方法(...)` 的形式调用无害的方法（`OS.get_name()`、`Engine.is_editor_hint()`、`ClassDB.class_exists()` 等）。它们不能被赋值、传递或索引，所以无法通过别名、`Engine.get_singleton()` 或 `ClassDB.instantiate()` 够到能启动进程的方法
- 拒绝 `Expression`、`JavaClassWrapper`、`JavaScriptBridge`、`GDExtensionManager`、`EditorSettings`，也拒绝访问助手自己的设置（权限）和编辑器的数据目录（凭据存储所在处）。`OS.get_environment()` 同样不在允许列表里，所以通过环境变量提供的密钥也够不到
- 拒绝网络调用（`request`、`connect_to_host`、`listen` 等）。为游戏创建联网节点没问题，从编辑器里发起网络访问不行
- `ProjectSettings.globalize_path()` 只接受 `DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://folder"))` 这一种写法；其他获取真实文件系统路径的途径（`OS.get_user_data_dir()` 等）一律拒绝
- 字符串字面量不能写绝对路径、UNC 或 `file://` 位置，也不能写用 `..` 跳出去的 `res://` / `user://` 路径
- 代码被拒绝时，原因会回传给模型，让它在规则之内重写

这是静态分析，不是沙箱。它看不到运行时才拼出来的值：用片段拼成的路径或方法名能通过检查。在你在意的机器或项目上运行前请先审阅生成的代码；想逐次批准的话，把“自动执行”关掉。

### 工具

服务商支持工具调用时（五家都支持），模型通过调用有类型参数的工具来操作项目，而不是写一段代码去执行。可在 *设置 > 常规 > 工具* 里关掉；拒绝工具调用的模型或端点会在第一次请求时被识别出来，此后改用旧方式。

| 类别 | 工具 | |
|------|------|-|
| 查看 | `get_scene_tree`、`get_node_properties`、`read_file`、`list_files`、`get_errors` | 不改动任何东西，不需要权限。 |
| 联网 | `web_search`、`fetch_url` | 到项目之外查资料：只允许公网 http(s) 地址，每条消息最多 3 次请求。返回的内容以“不可信内容”的标记交给模型。 |
| 修改 | `create_node`、`instantiate_scene`、`set_node_properties`、`rename_node`、`move_node`、`delete_node`、`connect_signal`、`attach_script`、`write_file`、`save_scene`、`set_project_setting`、`add_input_action` | 每个工具对应一个权限类别（*设置 > 权限*），按单次调用判定允许、询问或禁止。 |
| 宿主 | `run_scene`、`take_screenshot`、`run_editor_script`、`generate_image`、`generate_speech`、`remember` | 运行游戏几秒并回报错误、打印的内容以及（按需）一帧画面；把编辑器视口给模型看；执行任意编辑器代码；生成素材；往 `godot_ai.md` 里记一行。生成素材跟随“写入文件”权限；记笔记有自己的权限。 |

- **撤销。** 工具对场景的每次修改都是编辑器撤销历史里的一步，以所做的事命名：Ctrl+Z 撤回它，和手动修改一样。每条消息首次修改前会建一个检查点，聊天记录里出现一个链接，可撤销其后的全部修改，文件也包括在内。
- **值。** 参数是 JSON；引擎值可以按 GDScript 的写法写（`"Vector2(100, 50)"`）、写成分量数组（`[100, 50]`）或颜色码。资源写成 `res://` 路径，或就地新建：`{"$resource": "RectangleShape2D", "size": "Vector2(32, 32)"}`。值不能夹带代码：会构造对象的文本（`Object(GDScript, ...)`）不解析，脚本和场景资源不能就地新建，`script` 也不是工具能设置的属性。脚本只来自文件，经由 `write_file` 和 `attach_script`，受脚本权限约束；建节点时顺带挂脚本同样如此。
- **文件。** `read_file` 和 `write_file` 只在 `res://` 之内，跳过隐藏目录，不碰 `export_presets.cfg`（签名密钥）和编辑器自己管理的文件（`.tscn`、`.import`、`project.godot`）。解析不过的脚本不会写入。
- **问什么就给看什么。** `write_file` 的确认带着文件内容：新文件给全文，已有文件给删去和新增的行。`remember` 的确认带着笔记全文。
- **项目。** `set_project_setting` 只改已存在的设置，值按该设置的类型转换；自动加载和编辑器插件这类决定“哪些代码会运行”的设置留给用户自己改。`add_input_action` 用按键（按键盘上的物理位置，与输入映射一致）和鼠标键注册输入动作。两者都会保存 `project.godot`，检查点覆盖它。
- **运行中的游戏。** `run_scene` 回报游戏的报错和打印的内容；加上 `screenshot: true` 时，在停止前通过调试器连接向游戏要一帧画面。模型靠它检查一切“看了才知道”的结果；`take_screenshot` 给的是编辑器视口。
- **记笔记要询问。** `remember` 写下的一行会成为今后每次对话的指令的一部分，而促使模型去记的可能是它在文件或网页里读到的文字。因此“记笔记”是单独的权限，默认询问。
- **`run_editor_script`** 是兜底，用于没有工具能做的事（动画、瓦片地图、批量修改）。它和代码块一样经过语法、安全策略、权限三项审查，但审查读的是代码文本，而代码可以在运行时拼出审查要找的东西。所以由用户把关：确认弹窗的滚动视图里显示代码全文，用户同意后才运行，每次如此，与“自动执行”开关无关。它的结果就是工具的结果：代码 `print()` 的内容，或它抛出的错误。
- **代码块是给人看的。** 有工具的模型要运行代码就调用 `run_editor_script`。它回复里的代码块（讲解用的脚本、让用户自己在终端运行的命令）只显示，从不执行。
- **文字回答就是回答。** 对用户的文字回复（回答、拒绝、反问）就是最终结果，不会被催促“请输出代码块”。只有派给它去执行的步骤（计划步骤、运行时修复）才会再要求一次。
- **错误即结果。** 工具不会让整轮失败：出了什么问题会以文本形式交回模型处理。一条消息最多进行 8 轮工具调用。

#### 没有工具时（遗留路径）

五家服务商都支持工具调用，这条路只为拒绝工具调用的端点保留。它仍能工作，评测集也覆盖它，但新的工作不再放在这里：它需要三倍大的提示词、代码围栏解析器、对模型常犯错误的自动修正，以及一个把代码当文本读的安全检查。

没有工具调用的模型或端点仍按旧方式工作：它写一个 `gdscript` 代码块，经审查后按权限和“自动执行”设置执行；要联网、生成素材、记笔记时用文本标记（`[WEB_SEARCH: ...]`、`[FETCH_URL: ...]`、`[GENERATE_IMAGE: ...]`、`[GENERATE_AUDIO: ...]`、`[WRITE_MEMORY: ...]`）。对有工具的模型，既不讲解这些标记，也不从它的回复里读取。

### 权限系统

执行权限按操作类型分类，各自可配置：

- 创建节点
- 删除节点
- 修改属性
- 修改脚本
- 更改项目设置
- 写入文件
- 删除文件
- 记笔记

每一类可设为“允许”“询问”或“禁止”。删除文件默认禁止；修改脚本和记笔记默认询问：脚本是之后才运行的代码，脚本文件里的内容安全检查看不到；笔记则会进入今后每次对话的提示词。写入 `.gd` 文件算作修改脚本，挂脚本也是。

生成的代码够不到助手自己的开关：它的类、编辑器的界面树（`get_tree().root`、`EditorInterface.get_base_control()`、`Engine.get_main_loop()`）和控制器上的开关都会被安全检查拒绝；控制器上为无头评测准备的两个方法（`set_auto_approve`、`configure_provider`）只有在编辑器由 `evals/run_evals.py` 启动时才起作用。

只有完整的、确实是模型答案的回复才会被执行。回复在输出上限处被截断、被服务商的过滤器拦截、只有推理过程，或是一份计划，其中的代码只显示不运行。代码执行到一半失败时，先把场景和文件恢复到运行前的检查点，再让模型重试。

每段生成的代码都要查这张表，不论风险等级：“禁止”直接拦下，“询问”总会弹确认框，即使开着自动执行。关掉自动执行后，每次执行都要先确认。

类别是靠匹配生成代码里的 API 名字识别的。这能覆盖模型的正常输出；它不是沙箱，故意绕开匹配的代码能混过去。

### 上下文收集

助手可以从以下来源组织与编辑器相关的上下文：

- 当前场景树
- 选中的节点
- 项目结构
- 当前打开的脚本
- 输入框里提及的场景节点
- 附件的内容

### 上下文压缩

对话变长、token 用量超出当前模型的上下文窗口时会自动摘要。实现上保留最近的若干轮不动，把更早的历史压成滚动摘要。被压缩的只是发给模型的工作副本；完整历史仍保留，用于显示和保存的聊天记录。

三项设置控制它（设置对话框的“上下文压缩”部分）：

- **保留最近消息数**（2–20，默认 6）：最新的多少条消息永不压缩
- **压缩阈值**（30–95%，默认 70%）：历史窗口占满到多少时触发压缩。窗口等于模型上下文长度减去最大输出 token 数和系统提示词
- **AI 摘要**（默认关）：用一次后台请求让模型为被压缩的消息写摘要。这会消耗 token；关闭或请求失败时改用本地的启发式摘要

token 数在本地估算：中日韩字符每字一个，其他字符每四个算一个。

如果服务商仍以过长为由拒绝请求，会丢弃最早的消息并重试一次。

### 错误监视与自我恢复

- 在 AI 触发的执行窗口内捕获编辑器和运行时错误
- 把 AI 执行引起的错误与无关的控制台输出区分开
- 把失败反馈给助手以便自动重试
- 最多自动重试 3 次
- 监视运行中的游戏：运行的场景因运行时错误停止时，请助手修复并重新运行，最多 5 次。可在 设置 > 常规 里关掉；关掉自动执行时，每次修复都等待批准。错误文本以不可信数据的身份交给模型

### 检查点与回滚

- 有风险的修改之前创建场景检查点
- 保留最近 10 个检查点，创建时即写入磁盘，因此内置子资源（材质、形状）也能恢复
- 每次运行前还会为项目的文本文件（脚本、场景、资源、配置；每个不超过 512 KB）拍快照。回滚会恢复被代码修改或删除的文件，并把此后新建的文件移入系统回收站。大文件和二进制文件不在范围内
- 回滚是把项目退回到那个时间点，所以你在检查点之后对这些文件做的修改也会被撤销
- 可在面板界面里回滚

### 聊天历史

- 聊天会话以 JSON 文件保存在 `user://ai_assistant_chats`
- 支持列出、加载、删除保存的聊天

### 素材生成

通过 `generate_image` 和 `generate_speech` 工具（没有工具的模型用对应的文本标记）：

- 用 OpenAI 图像接口生成图像
- 用 OpenAI 语音合成接口生成音频

无论当前用的是哪家服务商，生成都需要 OpenAI 的 API 密钥；它与对话并行进行，保存到模型指定的路径：`res://` 内的一个新的图像或音频文件。工具会告诉模型文件此刻还不存在。

### 网页搜索与抓取

- 接入 DuckDuckGo 的 HTML 搜索
- 支持直接抓取网址，只限公网 http(s) 地址，每条用户消息最多 3 次网络请求。主机既按名字检查，也按它解析出的每个地址检查，随后只连接这些地址中的一个（不做第二次解析，所以检查和连接之间结果不会变；TLS 证书仍按主机名校验）。重定向逐跳跟随并经过同样的检查，因此公网页面无法把编辑器引向本机或内网地址。使用代理时（见[网络代理](#网络代理)）由代理解析主机名，经代理的抓取就只剩对网址本身的检查。
- 把 HTML 提取成文本，用于补充提示词

### 本地化

界面文字有英文和中文两种。

## 架构

智能体循环、会话、网络客户端和界面是分开的几块。停靠面板只显示控制器报告的内容、转发用户的操作，所以同一个控制器也能无界面运行（见[评测](#评测)）。

```text
AIAssistantPanel（停靠面板：控件、对话框）
 |- AIChatView             聊天记录、流式显示、链接
 |- AIMentionAutocomplete  “@” 补全弹窗
 |- AIRuntimeWatch         监视运行中的游戏，请求修复
 `- AIAgentController      以状态机实现的智能体循环
     |- AISession              历史、压缩、附件
     |- AIStreamClient         工作线程上的一次流式请求
     |- AICodeRunner           审查（语法、安全、权限）与执行
     |- AIPromptBuilder        系统提示词 = 知识 + 编辑器状态
     |- AIPlan                 所提计划的步骤及其清单文件
     |- AIWebRequester         回复要求的搜索 / 抓取
     |- AIHistorySummarizer    可选的、由模型撰写的历史摘要
     `- AIAssetService         图像 / 语音生成队列
```

`AIAgentController` 任何时候只处于一个状态，进行中的一切都属于同一个 `Run`：

```text
IDLE --提交 / 计划步骤 / 运行时修复--> STREAMING
STREAMING --回复含代码--> (AWAITING_PERMISSION --同意-->) 执行 --> VERIFYING
STREAMING --回复需要修正 / 执行失败--> STREAMING   （最多自动重试 3 次）
VERIFYING --迟到的错误--> STREAMING        VERIFYING --无错误--> 收尾
收尾 --联网标记--> WEB_REQUEST --结果--> STREAMING
STREAMING --回复调用工具--> 执行工具（视需要进入 AWAITING_PERMISSION / WEB_REQUEST）--结果--> STREAMING
收尾 --下一个计划步骤--> STREAMING        收尾 --其他--> IDLE
任意状态 --停止 / 出错 / 被拒绝--> IDLE
```

能否发起请求、代码能否运行、“停止”意味着什么，都由状态决定。一次运行的簿记（重试次数、待执行的代码、标记）是一个结构体，运行结束时整体重置，所以不会有东西漏到下一次运行里。

| 文件 | 职责 |
|------|------|
| `ui/ai_assistant_plugin.*` | 编辑器插件入口 |
| `ui/ai_assistant_panel.*` | 停靠面板：布局、用户操作、对话框 |
| `ui/ai_details_dialog.*`、`ui/ai_history_list.*` | “详情” / “思考过程”链接背后的窗口；保存的聊天列表 |
| `ui/ai_chat_view.*` | 聊天记录、流式显示、可点击的链接 |
| `ui/ai_markdown.*` | Markdown 转 RichTextLabel 的 BBCode，整段或流式；模型写的内容一律转义 |
| `ui/ai_mention_autocomplete.*` | 场景节点和项目文件的 `@` 补全弹窗 |
| `ui/ai_mention_text_edit.*`、`ui/ai_mention_highlighter.*` | 支持提及、带内联标签的输入框 |
| `ui/ai_settings_dialog.*`、`ui/ai_settings_dialog_model.cpp`、`ui/ai_settings_dialog_layout.cpp` | 设置窗口（模型、常规、权限、高级四个标签页）：显示与保存设置；控件与标题文字 |
| `providers/ai_provider_settings.*` | 编辑过程中的服务商设置，不含控件：各服务商的端点、输入和删除的密钥、建议的输出上限 |
| `common/ai_settings.*` | 读写 `ai_assistant/*` 设置的唯一位置；测试可给它换值 |
| `ui/ai_model_selector.*`、`network/ai_model_list_fetcher.*` | 带候选列表的模型名输入框；从服务商接口实时获取模型列表 |
| `providers/ai_provider_catalog.h`、`providers/ai_provider_catalog.cpp` | 各服务商的显示名和已知模型 |
| `agent/ai_agent_controller.*`、`agent/ai_agent_controller_replies.cpp` | 显式状态机形式的智能体循环；用信号而不是直接调用界面 |
| `agent/ai_agent_controller_tools.cpp` | 工具轮：权限、检查点、需要等待（运行游戏）或需要用户的工具 |
| `tools/ai_toolbox.*`、`tools/ai_tool_specs.cpp`、`tools/ai_edit_tools.cpp` | 工具：告诉模型的说明、查看类工具、修改类工具（以可撤销的编辑器动作实现） |
| `tools/ai_tool_values.*` | 工具参数（JSON）到引擎值和资源的转换 |
| `common/ai_tool_types.h` | 工具说明、工具调用，以及从流式片段拼出一次调用 |
| `common/ai_text_diff.*` | 文件两个版本之间按行的差异，用于确认弹窗 |
| `agent/ai_session.*` | 对话历史、上下文压缩、溢出恢复、附件去重 |
| `agent/ai_chat_store.*` | 保存的聊天（`user://ai_assistant_chats`） |
| `network/ai_stream_client.*`、`network/ai_stream_protocol.*` | 流式 HTTP 客户端；SSE 解码、UTF-8 边界、`<think>` 标签拆分 |
| `execution/ai_code_runner.*` | 审查关卡，以及带检查点和错误监视的执行 |
| `execution/ai_script_executor.*` | 把生成的 GDScript 编译并作为 `EditorScript` 运行 |
| `execution/ai_safety_policy.*` | 生成的代码不能包含什么，在文本和词法单元上检查 |
| `execution/ai_script_wrapper.*`、`execution/ai_code_fixes.*` | `EditorScript` 包装；对模型常犯错误的修正 |
| `execution/ai_permission_manager.*` | 基于规则的执行把关 |
| `execution/ai_checkpoint_manager.*` | 检查点与恢复 |
| `execution/ai_error_monitor.*` | 跟踪 AI 执行前后的编辑器和运行时错误，以及游戏的输出 |
| `agent/ai_runtime_watch.*` | 发现运行中游戏的运行时错误并请求修复 |
| `agent/ai_response_parser.*`、`agent/ai_response_analysis.*` | 代码块；副作用标记及对其内容的检查 |
| `network/ai_url_policy.*` | 模型给出的网址可以指向哪些地址 |
| `assets/ai_asset_request.*` | 一次生成请求及对保存路径的检查 |
| `common/ai_code_text.*`、`common/ai_notice.h` | 从代码和错误文本里读出的摘要与脚本路径；通知级别 |
| `agent/ai_plan.*` | 所提计划的步骤及其 Markdown 清单 |
| `network/ai_web_requester.*`、`network/ai_web_fetch.*`、`network/ai_web_search.*` | 网页搜索与抓取；抓取在工作线程上进行，只连接检查过的地址 |
| `assets/ai_asset_service.*`、`assets/ai_image_generator.*`、`assets/ai_audio_generator.*` | 图像与语音生成 |
| `network/ai_proxy.*`、`network/ai_proxy_os.cpp` | 助手的请求走哪个 HTTP 代理（系统、不用、自定义） |
| `network/ai_network_thread.h` | 标记网络工作线程，使其中的引擎报错不被当成项目的错误 |
| `agent/ai_history_summarizer.*` | 可选的、由模型撰写的压缩历史摘要 |
| `agent/ai_project_memory.*` | 模型要求记在 `res://godot_ai.md` 里的笔记 |
| `prompt/ai_prompt_builder.*`、`prompt/ai_context_collector.*`、`prompt/ai_profiler_collector.*` | 系统提示词的组装与编辑器上下文 |
| `prompt/ai_knowledge.*` | 加载知识文件（内置默认值 + 覆盖目录） |
| `prompt/ai_system_prompt.*`、`prompt/ai_gotchas_index.*`、`prompt/ai_domain_prompts.*`、`prompt/ai_api_doc_loader.*` | 从知识库里挑出一条消息需要的内容 |
| `providers/ai_provider.*`、`providers/ai_provider_setup.*`、`providers/` | 服务商接口、配置、各家实现 |
| `providers/ai_credential_store.*` | 设置文件之外的 API 密钥存储 |
| `common/ai_localization.h` | 界面文字的本地化表 |
| `knowledge/` | Markdown 形式的提示词、避坑经验、API 速查和领域指引 |
| `tests/` | 单元测试 |
| `evals/` | 任务集与无头运行器，用来给提示词和智能体的改动打分 |

`providers/` 里的服务商实现：

| 文件 | 服务商 |
|------|--------|
| `openai_compatible_provider.*` | 共用的 OpenAI chat-completions 实现 |
| `openai_provider.*` | OpenAI 及 OpenAI 兼容端点 |
| `deepseek_provider.*` | DeepSeek |
| `glm_provider.*` | 智谱 AI GLM |
| `anthropic_provider.*` | Anthropic Claude |
| `gemini_provider.*` | Google Gemini |

## 知识文件

告诉助手的一切非代码内容都在 `knowledge/` 里，是纯 Markdown：

| 路径 | 内容 |
|------|------|
| `system_prompt.md` | 基础系统提示词。`{{project_instructions}}` 标出项目的 `godot_ai.md` 插入的位置 |
| `prompts/<id>.md` | 带 `{{name}}` 占位符的短模板：工具说明、重试消息、计划步骤、运行时修复、网页结果、摘要、预设按钮 |
| `domains/<id>.md` | 某一领域的指引。文件头列出选中它的关键词（`match: stem` 或 `match: word`）和它在提示词里的顺序 `order` |
| `gotchas.md` | 每条以一行 `<!-- keywords: a, b -->` 开头；消息提到这些关键词时，其后的文字被注入 |
| `api_docs.md` | 每个类一节 `## 类名`，提到该类时注入 |
| `glossary.md` | 消息里可能出现的各种语言的说法，以及它们对应的引擎名字（`跳跃 => CharacterBody2D, move_and_slide`）。让没有提到类名的消息也能选中避坑经验和 API 速查 |

这些文件在构建时作为默认值嵌入编辑器二进制。想**不重新编译**就修改它们，让助手指向一个同样结构的目录：

- 编辑器设置 `ai_assistant/knowledge_dir`，或
- 环境变量 `GODOT_AI_KNOWLEDGE_DIR`，或
- 编辑器配置目录下的 `ai_assistant/knowledge` 文件夹（存在即自动使用）。

那里的文件会替换相对路径相同的内置文件；`domains/` 和 `gotchas/` 下多出来的文件则是追加。每次请求前都会检查该目录，所以改动在下一条消息生效。解析不了的文件会在编辑器输出里报告并跳过，内置内容保持不变。

开发提示词时，把 `ai_assistant/knowledge_dir` 设为本仓库的 `knowledge/` 文件夹，直接改文件即可。用评测集检验改动是否有帮助。

## API 密钥

密钥不存在编辑器设置文件里（`editor_settings-*.tres` 是明文，而且常被复制、同步、附在问题报告里）。每家服务商的密钥按以下顺序查找：

1. 环境变量 `GODOT_AI_API_KEY_<PROVIDER>`（例如 `GODOT_AI_API_KEY_DEEPSEEK`），其次是厂商惯用的变量：`ANTHROPIC_API_KEY`、`OPENAI_API_KEY`、`GEMINI_API_KEY`、`DEEPSEEK_API_KEY`、`ZHIPUAI_API_KEY`。
2. 凭据存储，即编辑器配置目录下的 `ai_assistant/credentials.json`。在设置对话框里输入的密钥存到这里。

Windows 上每个存入的密钥都用 DPAPI 封存，只有同一台机器上的同一个 Windows 用户才能读出。其他平台上用由机器标识派生的密钥做 AES 加密：这能让密钥不被一眼看到，文件拷到别处也没用，但它只是混淆，不是钥匙串，在这些平台上建议用环境变量。

旧版本留在设置文件里的密钥，会在面板第一次加载时移入凭据存储，设置里的条目随即清空。设置对话框从不显示已存的密钥：输入框是空的，只用来输入新密钥以替换已存的那个。

## 分层

各文件夹构成一个栈，从低到高：

```text
common < providers < network < execution < prompt < assets < tools < agent < ui
```

一个文件夹只能包含自己和排在它前面的文件夹里的头文件，不能包含后面的。编辑器设置只通过 `common/ai_settings.cpp` 访问（`ui` 文件夹另外负责把它们注册到编辑器）。`evals/check_layers.py` 检查这两条规则，并指出每一处违规的 `#include`：

```bash
python evals/check_layers.py
```

## 测试

单元测试覆盖不需要运行编辑器的逻辑：回复解析、副作用标记、网址与路径策略、安全策略与风险分级、避坑经验 / 领域 / API 速查的选取、知识文件的格式与覆盖、Markdown 渲染、设置与服务商设置模型、工具（在测试里搭的场景上）及各服务商的工具调用格式、历史压缩与溢出恢复、SSE 解码、各服务商的请求与响应处理、凭据存储。

```bash
scons platform=windows target=editor tests=yes
bin/godot.windows.editor.x86_64.console.exe --test --test-case="*AIAssistant*"
```

构建时加上 `extra_suffix=tests`，测试版的目标文件和二进制就与日常的编辑器构建分开，在两者之间切换不会触发完整重编。

### 持续集成

`.github/workflows/ci.yml` 检出引擎并把本模块放进去，在 Windows 上编译带测试的编辑器，运行单元测试、分层检查，以及用预设回复回放的评测集，这些都不需要 API 密钥。工作流里的 `GODOT_REF` 是所依据的引擎版本。发布包目前由本地构建后手动上传；工作流里的发布任务只能手动触发。

## 评测

助手好不好用取决于模型产出什么，而这会随着每一次对提示词或重试逻辑的改动而变化。`evals/` 里有一组标准任务（搭场景、写脚本、界面、3D、玩法、提问与计划、安全）和一个不需要人参与的打分运行器。详见 [evals/README.md](evals/README.md)。

```bash
# 测评测框架和智能体循环本身，用预设回复回放（不需要 API 密钥，约 2 分钟）：
python evals/run_evals.py --godot bin/godot.windows.editor.x86_64.console.exe --mock

# 真实模型；把一次提示词改动与之前的结果对比：
python evals/run_evals.py --godot <editor> --provider deepseek --out before.json
python evals/run_evals.py --godot <editor> --provider deepseek --knowledge-dir knowledge --baseline before.json
```

## 构建与安装

不想自己编译的话，[Releases](https://github.com/spardanviro/Godot_AI/releases) 里有带本模块的 Windows 版编辑器，解压后直接运行。其他平台目前需要自行编译。

### 1. 把模块放进 Godot 源码树

```text
godot/modules/ai_assistant/
```

本仓库就是模块目录，直接克隆或复制到 `modules/` 下。

### 2. 编译编辑器

```bash
scons platform=windows target=editor
```

按你的环境调整 `platform`、`arch` 和其他 SCons 参数。模块受 `TOOLS_ENABLED` 保护，只编进编辑器构建。

### 3. 启动自己编译的编辑器

编译完成后，用生成的编辑器可执行文件打开 Godot。AI 面板出现在右侧停靠区。

## 网络代理

助手发出的一切请求（对话、模型列表、摘要、图像和语音生成、网页搜索与抓取）走同一个代理选项（设置 > 高级）：

- **跟随系统**（默认）：编辑器自己的 `network/http_proxy` 设置（若已设置），否则是 `HTTPS_PROXY` / `HTTP_PROXY`，再否则在 Windows 上取用户账户配置的代理。不执行代理自动配置脚本。
- **不使用**：直连。
- **自定义**：`主机:端口`。

只支持 HTTP 代理（不支持 SOCKS，不支持代理认证）。本地地址（`localhost`、回环地址和内网地址字面量）始终直连，所以本地模型服务不受影响。

经代理抓取网页时无法检查主机名解析到哪个地址，因为解析是代理做的；对网址本身的检查（协议、本地主机名、地址字面量、每一跳重定向）仍然有效。如果这一点对你比访问那些必须经代理才能到达的网站更重要，把代理设为**不使用**。

## 配置

设置保存在 `EditorSettings` 的 `ai_assistant/*` 命名空间下。API 密钥不在其中，见 [API 密钥](#api-密钥)。

| 键 | 说明 |
|----|------|
| `ai_assistant/provider` | 当前服务商（`anthropic`、`openai`、`gemini`、`deepseek`、`glm`） |
| `ai_assistant/model` | 模型名 |
| `ai_assistant/api_endpoint` | 自定义端点 |
| `ai_assistant/custom_max_tokens` | 使用下面的最大 token 数，而不是模型的推荐值（默认关） |
| `ai_assistant/max_tokens` | 回复的最大 token 数，上一项开启时使用 |
| `ai_assistant/thinking` | 思考强度：留空为模型默认，`off`，模型提供的某一档，或以 token 计的预算 |
| `ai_assistant/auto_update_model_specs` | 本地模型参数超过一周时，从 models.dev 下载新的（默认开） |
| `ai_assistant/custom_temperature` | 发送下面的温度值，而不是交给模型决定（默认关） |
| `ai_assistant/temperature` | 采样温度，上一项开启时使用（默认 0.7） |
| `ai_assistant/send_on_enter` | 按 Enter 发送（默认开） |
| `ai_assistant/autorun` | 自动执行生成的代码（默认开） |
| `ai_assistant/use_tools` | 让模型用工具工作（默认开） |
| `ai_assistant/restore_last_chat` | 编辑器启动时恢复最近的聊天（默认开） |
| `ai_assistant/runtime_fix` | 自动修复运行中游戏的脚本错误（默认开） |
| `ai_assistant/compression_keep_recent` | 永不压缩的最近消息数（默认 6） |
| `ai_assistant/compression_threshold_pct` | 触发压缩的历史窗口占用比例（默认 0.7） |
| `ai_assistant/compression_use_ai_summary` | 用模型为被压缩的消息写摘要（默认关） |
| `ai_assistant/language` | 界面语言（`en` 或 `zh`） |
| `ai_assistant/knowledge_dir` | 用来覆盖内置知识文件的目录（默认空） |
| `ai_assistant/proxy_mode` | `system`（默认）、`off` 或 `custom` |
| `ai_assistant/proxy_address` | `custom` 模式下 HTTP 代理的 `主机:端口` |
| `ai_assistant/image_model` | OpenAI 图像模型（默认空，即 `gpt-image-1`） |
| `ai_assistant/speech_model` | OpenAI 语音模型（默认空，即 `gpt-4o-mini-tts`） |
| `ai_assistant/permissions/*` | 各类别的执行权限 |

## 项目级的 AI 指令

在项目里创建 `res://godot_ai.md`，写下该项目特有的指令。助手会自动加载这个文件并追加到系统提示词里。

## 使用流程

1. 从 AI 面板打开设置对话框。
2. 选择服务商并输入 API 密钥。
3. 需要的话填写端点和模型。
4. 选择 Enter 是否直接发送、生成的代码是否自动运行。
5. 为敏感操作配置权限级别。
6. 直接输入你想要的：提一个问题、要它做一件事，或要它先出计划。
7. 还可以这样丰富下一次请求：
   - 用 `+` 按钮附加文件
   - 用 `@` 提及场景节点或项目文件
   - 把场景节点、文件或文件夹拖进输入框

## 项目结构

```text
ai_assistant/
|- ui/            停靠面板、聊天视图、Markdown 渲染、提及、设置对话框
|- agent/         智能体循环、会话、保存的聊天、计划、回复的解析与分析
|- execution/     代码审查与执行、安全策略、权限、检查点、错误监视
|- prompt/        系统提示词组装、编辑器上下文、知识的加载与选取
|- network/       流式客户端、网页搜索与抓取、代理、模型列表
|- providers/     服务商接口、各家实现、目录、API 密钥存储
|- assets/        图像与语音生成
|- tools/         模型调用的工具：查看、修改及其参数转换
|- common/        设置访问、日志、本地化、通知级别、代码文本辅助、文本差异
|- knowledge/     提示词、避坑经验、API 速查、领域指引（Markdown）
|- tests/         单元测试（doctest）
|- evals/         评测任务、无头运行器、驱动脚本
|- ai_knowledge_builders.py   把 knowledge/ 嵌入构建
|- register_types.*
|- SCsub
`- config.py
```

## 目前的局限

- 所有大模型服务商都需要用户自备 API 密钥。
- 网页搜索靠抓取 HTML 实现，搜索页面改版时可能需要维护。
- 素材生成目前使用 OpenAI 专有的接口，无论当前用哪家服务商，都需要在设置里填 OpenAI 的 API 密钥。生成的素材只能保存在 `res://` 下。
- 停靠面板本身（布局、对话框）没有自动化测试覆盖；它背后的智能体循环有，通过评测运行器的回放模式。
- Windows 以外的平台上，凭据存储只是混淆，不是系统级保护。
- 预编译包目前只有 Windows 版；Linux 和 macOS 上的构建没有验证过。
- 工具调用用 DeepSeek 和 Claude（经 OpenAI 兼容与 Anthropic 两种格式）做过完整评测；GPT 系列和 Gemini 尚未实测。

## 许可

以 Apache License 2.0 授权，见 [LICENSE](LICENSE)。

这适用于本仓库中的代码。上游的 Godot 引擎以 MIT 许可授权。
